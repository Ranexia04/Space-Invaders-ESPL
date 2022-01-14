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

#include "AsyncIO.h"

#include "text.h"
#include "objects.h"
#include "pvp.h"

void vSendBulletState(char *bullet_state_tosend)
{
    if(aIOSocketPut(UDP, IPv4_addr, UDP_TRANSMIT_PORT, (char *)bullet_state_tosend, strlen(bullet_state_tosend))) {
        PRINT_ERROR("Failed to difficulty change to opponent");
    } else {
        prints("Sent bullet state change (%s) to opponent\n", bullet_state_tosend);
    }
}

void vComputeDiffString(char *tosend)
{
    int diff = my_spaceship.x - my_mothership.x;
    char number_str[4];

    if (diff < 0) {
        strcat(tosend, "-");
        diff = -1 * diff;
    } else {
        strcat(tosend, "+");
    }

    vGetNumberString(number_str, diff, 4);
    strcat(tosend, number_str);
}

void vSendSpaceshipMothershipDiff(void)
{
    char tosend[5] = {'\0'};

    vComputeDiffString(tosend);

    if(aIOSocketPut(UDP, IPv4_addr, UDP_TRANSMIT_PORT, tosend, strlen(tosend))) {
        PRINT_ERROR("Failed to position diference to opponent");
    } else {
        prints("Sent position diference (%s) to opponent\n", tosend);
    }
}

void vSendDifficultyChange(char *tosend)
{
    if(aIOSocketPut(UDP, IPv4_addr, UDP_TRANSMIT_PORT, (char *)tosend, strlen(tosend))) {
        PRINT_ERROR("Failed to difficulty change to opponent");
    } else {
        prints("Sent difficulty change (%s) to opponent\n", tosend);
    }
}

void vReceiveCallback(size_t receive_size, char *buffer, void *args)
{
    if (vCheckCanReceiveData()) {
        if (strcmp(buffer, "INC") == 0) {
            my_mothership.direction = LEFT_TO_RIGHT;
            prints("Received order to increment position\n");
        }
        if (strcmp(buffer, "DEC") == 0) {
            my_mothership.direction = RIGHT_TO_LEFT;
            prints("Received order to decrement position\n");
        }
        if (strcmp(buffer, "HALT") == 0) {
            my_mothership.direction = STOP;
            prints("Received order to stop\n");
        }
        xSemaphoreGive(my_mothership.lock);
    }
}

void vInitPVP(void)
{
    UDP_receive_handle = aIOOpenUDPSocket(IPv4_addr, UDP_RECEIVE_PORT, 2000, 
            vReceiveCallback, NULL);
    //UDP_transmit_handle = aIOOpenUDPSocket(IPv4_addr, UDP_TRANSMIT_PORT, 2000, 
            //NULL, NULL);
}