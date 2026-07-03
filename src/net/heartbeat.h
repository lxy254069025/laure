#ifndef LAURE_HEARTBEAT_H
#define LAURE_HEARTBEAT_H

#include "net/conn.h"

void laure_hb_start(laure_conn_t *c);
void laure_hb_stop(laure_conn_t *c);
void laure_hb_reset(laure_conn_t *c);
#endif