#ifndef __TEXT__
#define __TEXT__

#define NOT_CENTERING 0
#define CENTERING 1

#define TEXT_COLOUR White

extern void checkDraw(unsigned char status, const char *msg);

/**
 * @brief Add spaces to between each char in str.
 * 
 * @param str String to be modified.
 */
void vAddSpaces(char *str);

/**
 * @brief Converts int number into string.
 * If number has more digits than n_digits
 * then str becomes max number for that number
 * of digits. (e.g. if number == 10000 and n_digits == 4)
 * str = "9999".
 * 
 * @param str 
 * @param number 
 * @param n_digits 
 */
void vGetNumberString(char *str, int number, int n_digits);

/**
 * @brief Draws a string on screen on selected location.
 * 
 * @param buffer String to draw
 * @param x x coordinate to draw on
 * @param y y coordinate to draw on
 * @param centering If 1, then subtracts text width and height
 * to x and y coordinates, respectively.
 */
void vDrawText(char *buffer, int x, int y, int centering);

/**
 * @brief Computes max number for the number of digits input.
 * (e.g. if n_digits == 4, max_number = 9999)
 * 
 * @param max_number 
 * @param n_digits 
 */
void vGetMaxNumber(int *max_number, int n_digits);

/**
 * @brief Draws an int on the screen.
 * 
 * @param number int to be drawn
 * @param x x coordinate to draw on
 * @param y y coordinate to draw on
 * @param n_digits Number of digits to be drawn.
 */
void vDrawNumber(int number, int x, int y, int n_digits);

#endif