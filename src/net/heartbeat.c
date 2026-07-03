#include "heartbeat.h"
#include "net/conn.h"
#include "uv.h"
#include "websocket.h"

static void hb_timer_cb(uv_timer_t *t) {
    laure_conn_t   *c   = (laure_conn_t *)t->data;
    laure_server_t *srv = c->server;
    if (c->state != LAURE_CONN_WS_OPEN) {
        laure_hb_stop(c);
        return;
    }

    if (c->hb_waiting_pong) {
        laure_ws_close(c, 1001, "heartbeat timeout");
        laure_conn_close(c);
        return;
    }

    laure_ws_ping(c, "hb", 2);
    c->hb_waiting_pong = 1;
    uv_timer_start(t, hb_timer_cb, srv->hb_timeout_ms, 0);
}

void laure_hb_start(laure_conn_t *c) {
    if (c->hb_active)
        return;
    uv_timer_init(c->server->loop, &c->hb_timer);
    c->hb_timer.data   = c;
    c->hb_active       = 1;
    c->hb_waiting_pong = 0;
    uv_timer_start(&c->hb_timer, hb_timer_cb, c->server->hb_interval_ms, 0);
}

void laure_hb_stop(laure_conn_t *c) {
    if (!c->hb_active)
        return;
    uv_timer_stop(&c->hb_timer);
    if (!uv_is_closing((uv_handle_t *)&c->hb_timer)) {
        uv_close((uv_handle_t *)&c->hb_timer, NULL);
    }
    c->hb_active = 0;
}

void laure_hb_reset(laure_conn_t *c) {
    c->hb_waiting_pong = 0;
    if (c->server && c->server->loop) {
        c->hb_last_pong_ms = uv_now(c->server->loop);
    }
}