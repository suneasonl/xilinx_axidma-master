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

#define MAX_LEN 800
#define VALUE 788
#define LEN 10
// A convenient structure to carry information around about the transfer
struct dma_transfer {
    int input_channel;      // The channel used to send the data
    int *tx_buf;              // The buffer to transfer data
    int output_channel;     // The channel used to receive the data
    int *rx_buf;             // The buffer to receive data
};

// /* Parses the command line arguments overriding the default transfer sizes,
//  * and number of transfer to use for the benchmark if specified. */
static int parse_args(int *input_channel, int *output_channel)
{
    *input_channel = -1;
    *output_channel = -1;

    // If one of -t or -r is specified, then both must be
    if ((*input_channel == -1) ^ (*output_channel == -1)) {
        fprintf(stderr, "Error: Either both -t and -r must be specified, or "
                "neither.\n");
        return -EINVAL;
    }
    return 0;
}

/*----------------------------------------------------------------------------
 * DMA Data Transfer Functions
 *----------------------------------------------------------------------------*/

static int transfer_data(axidma_dev_t dev, struct dma_transfer *trans)
{
    int rc = 0;

    // Allocate a buffer for the input data, and read it into the buffer
    trans->tx_buf = axidma_malloc(dev,MAX_LEN);
    if (trans->tx_buf == NULL) {
        rc = -ENOMEM;
    }
    //initial the tx_buf
    for(int i=0; i<LEN ;i++){
        trans->tx_buf[i] = VALUE + i;
    }
    // Allocate a buffer for the output data, and read it into the buffer
    trans->rx_buf = axidma_malloc(dev, MAX_LEN);
    if (trans->rx_buf == NULL) {
        fprintf(stderr, "Failed to allocate the input buffer.\n");
        rc = -ENOMEM;
    }
    for(int i=0; i < LEN ;i++){
        trans->rx_buf[i] = 0;
    }
    // Perform the transfer
    // Perform the main transaction
    rc = axidma_twoway_transfer(dev, trans->input_channel, trans->tx_buf,
            MAX_LEN, trans->output_channel, trans->rx_buf, MAX_LEN, true);
    if (rc < 0) {
        fprintf(stderr, "DMA read write transaction failed.\n");
    }

    //loopback test
    printf("start the loopback test\n");
    for (int Index = 0; Index < LEN ; Index++){
    	printf("ps:%d == pl: %d \n",VALUE+Index, trans->rx_buf[Index]);
    }

    return rc;
}

int main()
{
    int rc;
    axidma_dev_t axidma_dev;
    struct dma_transfer trans;
    const array_t *tx_chans, *rx_chans;

    // Parse the input arguments(initialize)
    memset(&trans, 0, sizeof(trans));
    if (parse_args(&trans.input_channel,&trans.output_channel) < 0) {
        rc = 1;
        goto ret;
    }

    // Initialize the AXIDMA device
    axidma_dev = axidma_init();
    if (axidma_dev == NULL) {
        fprintf(stderr, "Error: Failed to initialize the AXI DMA device.\n");
        rc = 1;
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

    // Transfer data over the AXI DMA
    rc = transfer_data(axidma_dev, &trans);

destroy_axidma:
    axidma_destroy(axidma_dev);
ret:
    return rc;
}