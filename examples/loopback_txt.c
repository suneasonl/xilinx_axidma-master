/**
 * @file loop_back.c
 * @date 2021.6.28
 * @author lishaocong
 
 **/

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <fcntl.h>              // Flags for open()
#include <sys/stat.h>           // Open() system call
#include <sys/types.h>          // Types for open()
#include <unistd.h>             // Close() system call
#include <string.h>             // Memory setting and copying
#include <getopt.h>             // Option parsing
#include <errno.h>              // Error codes

#include "util.h"               // Miscellaneous utilities
#include "conversion.h"         // Convert bytes to MBs
#include "libaxidma.h"          // Interface ot the AXI DMA library

/*----------------------------------------------------------------------------
 * Internal Definitions
 *----------------------------------------------------------------------------*/

// A convenient structure to carry information around about the transfer
struct dma_transfer {
    int input_fd;           // The file descriptor for the input file
    int input_channel;      // The channel used to send the data
    int input_size;         // The amount of data to send
    void *input_buf;        // The buffer to hold the input data
    int output_fd;          // The file descriptor for the output file
    int output_channel;     // The channel used to receive the data
    int output_size;        // The amount of data to receive
    void *output_buf;       // The buffer to hold the output
};

// /* Parses the command line arguments overriding the default transfer sizes,
//  * and number of transfer to use for the benchmark if specified. */
static int parse_args(char **input_path, char **output_path, 
int *input_channel, int *output_channel, int *output_size)
{
    bool o_specified, s_specified;
    *input_channel = -1;
    *output_channel = -1;
    *output_size = -1;
    o_specified = false;
    s_specified = false;

    // If one of -t or -r is specified, then both must be
    if ((*input_channel == -1) ^ (*output_channel == -1)) {
        fprintf(stderr, "Error: Either both -t and -r must be specified, or "
                "neither.\n");
        // print_usage(false);
        return -EINVAL;
    }

    // Only one of -s and -o can be specified
    if (s_specified && o_specified) {
        fprintf(stderr, "Error: Only one of -s and -o can be specified.\n");
        // print_usage(false);
        return -EINVAL;
    }

    // Parse out the input and output paths
    *input_path = "/media/sd-mmcblk0p1/tx.txt";
    *output_path = "/media/sd-mmcblk0p1/rx.txt";
    return 0;
}

/*----------------------------------------------------------------------------
 * DMA File Transfer Functions
 *----------------------------------------------------------------------------*/

static int transfer_file(axidma_dev_t dev, struct dma_transfer *trans,
                         char *output_path)
{
    int rc;
    // Allocate a buffer for the input file, and read it into the buffer
    trans->input_buf = axidma_malloc(dev, trans->input_size);
    if (trans->input_buf == NULL) {
        fprintf(stderr, "Failed to allocate the input buffer.\n");
        rc = -ENOMEM;
        goto ret;
    }
    rc = robust_read(trans->input_fd, trans->input_buf, trans->input_size);
    if (rc < 0) {
        perror("Unable to read in input buffer.\n");
        axidma_free(dev, trans->input_buf, trans->input_size);
        return rc;
    }

    // Allocate a buffer for the output file
    trans->output_buf = axidma_malloc(dev, trans->output_size);
    if (trans->output_buf == NULL) {
        rc = -ENOMEM;
        goto free_input_buf;
    }

    // Perform the transfer
    // Perform the main transaction
    rc = axidma_twoway_transfer(dev, trans->input_channel, trans->input_buf,
            trans->input_size, trans->output_channel, trans->output_buf,
            trans->output_size, true);
    if (rc < 0) {
        fprintf(stderr, "DMA read write transaction failed.\n");
        goto free_output_buf;
    }

    // Write the data to the output file
    printf("Writing output data to `%s`.\n", output_path);
    rc = robust_write(trans->output_fd, trans->output_buf, trans->output_size);

free_output_buf:
    axidma_free(dev, trans->output_buf, trans->output_size);
free_input_buf:
    axidma_free(dev, trans->input_buf, trans->input_size);
ret:
    return rc;
}

/*----------------------------------------------------------------------------
 * Main
 *----------------------------------------------------------------------------*/

int main()
{
    int rc;
    char *input_path, *output_path;
    axidma_dev_t axidma_dev;
    struct stat input_stat;
    struct dma_transfer trans;
    const array_t *tx_chans, *rx_chans;

    // Parse the input arguments
    memset(&trans, 0, sizeof(trans));
    if (parse_args(&input_path, &output_path, &trans.input_channel,
                   &trans.output_channel, &trans.output_size) < 0) {
        rc = 1;
        goto ret;
    }

    // Try opening the input and output images
    trans.input_fd = open(input_path, O_RDONLY);
    if (trans.input_fd < 0) {
        perror("Error opening input file");
        rc = 1;
        goto ret;
    }
    trans.output_fd = open(output_path, O_WRONLY|O_CREAT|O_TRUNC,
                     S_IWUSR|S_IRUSR|S_IRGRP|S_IWGRP|S_IROTH);
    if (trans.output_fd < 0) {
        perror("Error opening output file");
        rc = -1;
        goto close_input;
    }

    // Initialize the AXIDMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL) {
        fprintf(stderr, "Error: Failed to initialize the AXI DMA device.\n");
        rc = 1;
        goto close_output;
    }

    // Get the size of the input file
    if (fstat(trans.input_fd, &input_stat) < 0) {
        perror("Unable to get file statistics");
        rc = 1;
        goto destroy_axidma;
    }

    // If the output size was not specified by the user, set it to the default
    trans.input_size = input_stat.st_size;
    if (trans.output_size == -1) {
        trans.output_size = trans.input_size;
    }

    // Get the tx and rx channels if they're not already specified
    tx_chans = axidma_get_dma_tx(axidma_dev);
    if (tx_chans->len < 1) {
        fprintf(stderr, "Error: No transmit channels were found.\n");
        rc = -ENODEV;
        goto destroy_axidma;
    }
    rx_chans = axidma_get_dma_rx(axidma_dev);
    if (rx_chans->len < 1) {
        fprintf(stderr, "Error: No receive channels were found.\n");
        rc = -ENODEV;
        goto destroy_axidma;
    }

    /* If the user didn't specify the channels, we assume that the transmit and
     * receive channels are the lowest numbered ones. */
    if (trans.input_channel == -1 && trans.output_channel == -1) {
        trans.input_channel = tx_chans->data[0];
        trans.output_channel = rx_chans->data[0];
    }
    printf("AXI DMA File Transfer Info:\n");
    printf("\tTransmit Channel: %d\n", trans.input_channel);
    printf("\tReceive Channel: %d\n", trans.output_channel);
    printf("\tInput File Size: %.2f Mb\n", BYTE_TO_MB(trans.input_size));
    printf("\tOutput File Size: %.2f Mb\n\n", BYTE_TO_MB(trans.output_size));

    // Transfer the file over the AXI DMA
    rc = transfer_file(axidma_dev, &trans, output_path);
    rc = (rc < 0) ? -rc : 0;

destroy_axidma:
    axidma_destroy(axidma_dev);
close_output:
    assert(close(trans.output_fd) == 0);
close_input:
    assert(close(trans.input_fd) == 0);
ret:
    return rc;
}
