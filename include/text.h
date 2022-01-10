#ifndef __TEXT__
#define __TEXT__

#define NOT_CENTERING 0
#define CENTERING 1

#define TEXT_COLOUR White

void checkDraw(unsigned char status, const char *msg);

void vAddSpaces(char *str);

void vGetNumberString(char *str, int number, int n_digits);

void vDrawText(char *buffer, int x, int y, int centering);

void vGetMaxNumber(int *max_number, int n_digits);

void vDrawNumber(int number, int x, int y, int n_digits);

#endif