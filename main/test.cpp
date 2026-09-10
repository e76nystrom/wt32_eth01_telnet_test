//
// Created by Eric Nystrom on 7/2/26.
//
#include <cstdio>

extern "C" {
void test()
{
printf("Hello World!\n");
}
}

void printHex(const uint8_t *data, size_t len)
{
 int col = 0;
 for (size_t i = 0; i < len; i++)
 {
  if (col == 0)
  {
   printf("  %04X: ", static_cast<unsigned int>(i));
  }
  printf("%02X ", data[i]);
  col += 1;
  if (col == 16)
  {
   col = 0;
   printf("\n");
  }
 }
 if (col != 0)
  printf("\n");
}

char* nextArg(char* p0)
{
 while (true)
 {
  const char c0 = *p0;
  if (c0 == 0)
   break;
  p0 += 1;
  if (c0 == ',')
  {
   break;
  }
 }
 return p0;
}

char *getNum(char *p0, int n, int *result)
{
 int val = 0;
 while (n > 0)
 {
  const char c1 = *p0++;
  val *= 10;
  val += c1 - '0';
  n -= 1;
 }
 *result = val;
 return p0;
}

int getNum(char **p0, int n)
{
 char *p1 = *p0;
 int val = 0;
 while (n > 0)
 {
  const char c1 = *p1++;
  val *= 10;
  val += c1 - '0';
  n -= 1;
 }
 *p0 = p1;
 return val;
}
