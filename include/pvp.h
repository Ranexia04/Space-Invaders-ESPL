#ifndef __PVP__
#define __PVP__

#define UDP_RECEIVE_PORT 1234
#define UDP_TRANSMIT_PORT 1235
#define IPv4_addr "127.0.0.1"

extern aIO_handle_t UDP_receive_handle;
extern aIO_handle_t UDP_transmit_handle;

extern int vCheckCanReceiveData(void);

void vSendBulletState(char *bullet_state_tosend);

void vSendSpaceshipLocation(void);

void vSendDifficultyChange(char *tosend);

void vReceiveCallback(size_t receive_size, char *buffer, void *args);

void vInitPVP(void);

#endif
