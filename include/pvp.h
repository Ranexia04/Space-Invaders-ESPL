#ifndef __PVP__
#define __PVP__

#define UDP_RECEIVE_PORT 1234
#define UDP_TRANSMIT_PORT 1235
#define IPv4_addr "127.0.0.1"

extern aIO_handle_t UDP_receive_handle;
extern aIO_handle_t UDP_transmit_handle;

extern int vCheckCanReceiveData(void);

/**
 * @brief Sends string containing bullet state to opponent.
 * 
 * @param bullet_state_tosend String to send.
 */
void vSendBulletState(char *bullet_state_tosend);

/**
 * @brief Sends string containing player and opponent
 * location difference signed change to opponent.
 * 
 */
void vSendSpaceshipMothershipDiff(void);

/**
 * @brief Sends string containing difficulty change to opponent.
 * 
 * @param tosend String to send.
 */
void vSendDifficultyChange(char *tosend);

/**
 * @brief Callback function when data is received.
 * Checks if player is in game and if PVP mode is activated.
 * If so then uses the received information.
 * 
 * @param receive_size 
 * @param buffer String containing received data.
 * @param args 
 */
void vReceiveCallback(size_t receive_size, char *buffer, void *args);

/**
 * @brief Opens socket to receive information from opponent.
 * 
 */
void vInitPVP(void);

#endif
