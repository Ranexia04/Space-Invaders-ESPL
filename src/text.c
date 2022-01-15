#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "queue.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

#include "TUM_Ball.h"
#include "TUM_Draw.h"
#include "TUM_Font.h"
#include "TUM_Event.h"
#include "TUM_Sound.h"
#include "TUM_Utils.h"
#include "TUM_FreeRTOS_Utils.h"
#include "TUM_Print.h"

#include "text.h"

void vAddSpaces(char *str)
{
    int i = 0;
    char buffer[30], buffer2[30];

    //puts string in buffer and resets string
    strcpy(buffer, str);
    strcpy(str, "");

    //adds each letter followed by a space to str
    while (buffer[i] != '\0') {
        sprintf(buffer2, "%c ", buffer[i]);
        strcat(str, buffer2);
        i++;
    }
}

void vGetNumberString(char *str, int number, int n_digits)
{
    char num_str[30];
	int index = 0, i;
    
    sprintf(num_str, "%d", number);
    while ( num_str[index] != '\0' ) {
        index++;
    }
    index--;//index is now the index of the last digit of the number we want to draw
    
    i = n_digits;
    str[i] = '\0';
    i--;//i is now at the place where we put last digit

    //puts each number in the final string
    while (index >= 0) {
        str[i] = num_str[index];
        index--;
        i--;
    }//completes the left part of the string with zeros
    while (i >= 0) {
        str[i] = '0';
        i--;
    }
}

void vDrawText(char *buffer, int x, int y, int centering)
{
    char str[30] = {'\0'};
	int text_width;

    strcpy(str, buffer);
    vAddSpaces(str);
	tumGetTextSize(str, &text_width, NULL);
    if (centering == CENTERING) {
        x = x - text_width / 2;
        y = y - DEFAULT_FONT_SIZE / 2;
    }
	checkDraw(tumDrawText(str, x, y, TEXT_COLOUR), __FUNCTION__);
}

void vGetMaxNumber(int *max_number, int n_digits)
{
    //adds 9, 90, 900 etc depending on the number of digits
    for ( int i = 0; i < n_digits; i++) {
        (*max_number) = (*max_number) + 9 * pow(10, i);
    }
}

void vDrawNumber(int number, int x, int y, int n_digits)
{
    char str[30];
    int max_number = 0;

    vGetMaxNumber(&max_number, n_digits);

    if ( number > max_number ) {
        sprintf(str, "%d", max_number);
    } else {
        vGetNumberString(str, number, n_digits);
    }
    
    vAddSpaces(str);
	vDrawText(str, x, y, NOT_CENTERING);
}