#include <stdio.h>
void CreateMyFile(char * szFileName,int nFileLength)
{
FILE* fp = fopen(szFileName, "wb+"); // 创建文件
if(fp==NULL)
printf("文件打开失败");
else
{
fseek(fp, nFileLength-1, SEEK_SET); // 将文件的指针 移至 指定大小的位�?
fputc(32, fp); // 在要指定大小文件的末尾随便放一个数�?
fclose(fp);
}
}
void main()
{
CreateMyFile("rx.txt",1024*1); //调用测试
CreateMyFile("tx.txt",1024*31000); //调用测试
}