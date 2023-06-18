// -*- mode: c; tab-width: 4; indent-tabs-mode: 1; st-rulers: [70] -*-
// vim: ts=8 sw=8 ft=c noet

#include <stddef.h>
#include <stdlib.h>
#include <uv.h>

#include "async_io.h"
#include "log.h"
#include "redd.h"
#include "helper.h"

static void async_io_worker_poll_cb(uv_poll_t *poll, int status, int events);
static int async_io_worker_poll_start(async_io_t *async_io);

static void
async_io_loop_do(void *data, async_io_queue_t *async_io_queue)
{
	ngx_queue_t *q;
	async_io_buf_t *buf;
	while(!ngx_queue_empty(&async_io_queue->work_queue)) {
		q = ngx_queue_head(&async_io_queue->work_queue);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
		if (!async_io_queue->_each(data, buf)) {
			break;
		}
		ngx_queue_remove(q);
		ngx_queue_insert_tail(&async_io_queue->work_done, q);
	}
}

static void
async_io_do(uv_work_t *req)
{
	async_io_baton_t *baton = (async_io_baton_t*)req->data;
	void *data = baton->async_io_queue->data;
	async_io_queue_t *async_io_queue = baton->async_io_queue;

	ngx_queue_t *q;
	async_io_buf_t *buf;
	while(!ngx_queue_empty(&async_io_queue->work_queue)) {
		q = ngx_queue_head(&async_io_queue->work_queue);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
		if (!async_io_queue->_each(data, buf)) {
			break;
		}
		ngx_queue_remove(q);
		ngx_queue_insert_tail(&async_io_queue->work_done, q);
	}
}

static void
async_io_loop_callback(void *data, async_io_queue_t *async_io_queue, int status)
{
	ngx_queue_t *q;
	async_io_buf_t *buf;

	while(!ngx_queue_empty(&async_io_queue->work_done)) {
		q = ngx_queue_head(&async_io_queue->work_done);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
		if (async_io_queue->_done)
			async_io_queue->_done(data, buf);
		ngx_queue_remove(q);
		ngx_queue_insert_tail(&async_io_queue->avail_queue, q);
	}

	if (async_io_queue->_cb)
		async_io_queue->_cb(data, status);
}

static void
async_io_callback(uv_work_t *req, int status)
{
	async_io_baton_t *baton = (async_io_baton_t*)req->data;
	void *data = baton->async_io_queue->data;
	async_io_queue_t *async_io_queue = baton->async_io_queue;

	ngx_queue_t *q;
	async_io_buf_t *buf;

	while(!ngx_queue_empty(&async_io_queue->work_done)) {
		q = ngx_queue_head(&async_io_queue->work_done);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
		if (async_io_queue->_done)
			async_io_queue->_done(data, buf);
		ngx_queue_remove(q);
		ngx_queue_insert_tail(&async_io_queue->avail_queue, q);
	}

	if (async_io_queue->_cb)
		async_io_queue->_cb(data, status);

	async_io_queue->working = 0;
	async_io_poll_start(baton->async_io);
}

static void
async_io_poll_cb(uv_poll_t *poll, int status, int events)
{
	async_io_t *async_io = (async_io_t *)poll->data;
	async_io_poll_stop(async_io);
	if (events & UV_READABLE) {

		async_io->read_io.working = 1;
		async_io_baton_t *baton = &async_io->read_io.baton;
		baton->async_io_queue = &async_io->read_io;
		baton->async_io = async_io;
		baton->events = UV_READABLE;
		baton->req.data = (void *)baton;
		ngx_empty_into(&async_io->read_io.avail_queue, &async_io->read_io.work_queue,32);
		uv_queue_work(server.loop, &baton->req, async_io_do, async_io_callback);
	}
	if (events & UV_WRITABLE) {
		async_io->write_io.working = 1;
		async_io_baton_t *baton = &async_io->write_io.baton;
		baton->async_io_queue = &async_io->write_io;
		baton->async_io = async_io;
		baton->events = UV_WRITABLE;
		baton->req.data = (void *)baton;
		ngx_empty_into(&async_io->write_io.ready_queue, &async_io->write_io.work_queue,32);
		uv_queue_work(server.loop, &baton->req, async_io_do, async_io_callback);
	}
}

static int
async_io_poll_init(async_io_t *async_io, uv_loop_t *loop,int (*init_cb)(async_io_t *async_io))
{
	async_io->poll.data = (void *)async_io;
	int retval = uv_poll_init(loop, &async_io->poll, async_io->fd);
	if (retval != UV_OK) {
		uv_err_t error = uv_last_error(loop);
		red_log(REDD_WARNING, "FATAL uv_poll_init %s: %s\n", uv_err_name(error), uv_strerror(error));
		return retval;
	}

	return init_cb(async_io);
}

static void
async_io_queue_init(async_io_queue_t *async_io_queue, int fd, int flags, void *data,
	int buf_len, int buf_count,
	async_io_each _each, async_io_done _done, async_io_cb _cb)
{
	async_io_queue->fd = fd;
	async_io_queue->flags = flags;
	async_io_queue->data = data;
	ngx_queue_init(&async_io_queue->avail_queue);
	ngx_queue_init(&async_io_queue->ready_queue);
	ngx_queue_init(&async_io_queue->work_queue);
	ngx_queue_init(&async_io_queue->work_done);

	async_io_queue->_each = _each;
	async_io_queue->_done = _done;
	async_io_queue->_cb = _cb;

	async_io_queue->buf_len = buf_len;
	async_io_queue->buf_count = buf_count;

	int i;
	for (i = 0; i < buf_count; i++) {
		async_io_buf_t *buf = async_io_buf_create(buf_len);
		ngx_queue_insert_tail(&async_io_queue->avail_queue, &buf->queue);
	}
}

async_io_buf_t
*async_io_buf_create(int len)
{
	async_io_buf_t *buf = (async_io_buf_t *)malloc(sizeof(async_io_buf_t));
	ngx_queue_init(&buf->queue);
	buf->len = len;
	buf->maxlen = len;
	buf->buf = (char *)malloc(len);
	return buf;
}

async_io_buf_t
*async_io_write_buf_get(async_io_t *async_io)
{
	async_io_buf_t *buf = NULL;

	if (ngx_queue_empty(&async_io->write_io.avail_queue)) {
		return NULL;
	} else {
		ngx_queue_t *q;
		q = ngx_queue_last(&async_io->write_io.avail_queue);
		ngx_queue_remove(q);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
	}

	return buf;
}

int
async_io_init(async_io_t *async_io, int fd, void *data,
	int read_buf_len, int read_buf_count,
	async_io_each read_each, async_io_done read_done, async_io_cb read_cb,
	int write_buf_len, int write_buf_count,
	async_io_each write_each, async_io_done write_done, async_io_cb write_cb)
{
	async_io->fd = fd;
	async_io_queue_init(&async_io->read_io, fd, UV_READABLE, data,
		read_buf_len, read_buf_count,
		read_each, read_done, read_cb);
	async_io_queue_init(&async_io->write_io, fd, UV_WRITABLE, data,
		write_buf_len, write_buf_count,
		write_each, write_done, write_cb);

	return async_io_poll_init(async_io,server.loop,async_io_poll_start);
}

int
async_io_poll_stop(async_io_t *async_io)
{
	return uv_poll_stop(&async_io->poll);
}

int
async_io_poll_start(async_io_t *async_io)
{
	int retval = UV_OK;
	int flags = 0;
	if (!async_io->read_io.working) {
		flags |= UV_READABLE;
	}
	if (!async_io->write_io.working) {
		if (!ngx_queue_empty(&async_io->write_io.ready_queue)) {
			flags |= UV_WRITABLE;
		}
	}

	if (flags) {
		retval = uv_poll_start(&async_io->poll, flags, async_io_poll_cb);
	} else {
		async_io_poll_stop(async_io);
	}

	return retval;
}

/* secondary thread */
static void
send_read_buffers(async_io_t *async_io)
{
	if(async_io->read_io.working)
		return;

	ngx_empty_into(&async_io->read_io.work_done,&async_io->read_io.send_queue,-1);

	if(!ngx_queue_empty(&async_io->read_io.send_queue)){
		async_io->read_io.working = true;
		uv_async_send(&async_io->read_io.ping);
	}else{
		async_io_worker_poll_start(async_io);
	}
}

/* main thread */
static void
read_ping(uv_async_t *handle, int status)
{
	async_io_t *async_io = (async_io_t *)handle->data;
	async_io_queue_t *queue = (async_io_queue_t *)&async_io->read_io;

	ngx_queue_t *q;
	ngx_queue_foreach(q,&queue->send_queue){
		async_io_buf_t *packet = ngx_queue_data(q,async_io_buf_t,queue);
		queue->_done(queue->data,packet);
	}
	uv_async_send(&queue->pong);
}

/* secondary thread */
static void
read_pong(uv_async_t *handle, int status)
{
	async_io_t *async_io = (async_io_t *)handle->data;
	async_io_queue_t *queue = (async_io_queue_t *)&async_io->read_io;

	queue->_cb(queue->data,1);
	ngx_empty_into(&queue->send_queue,&queue->avail_queue,-1);

	queue->working = false;
	send_read_buffers(async_io);
}

void
send_write_buffers(async_io_queue_t *queue)
{
	if(queue->working)
		return;

	ngx_empty_into(&queue->ready_queue,&queue->send_queue,32);

	if(!ngx_queue_empty(&queue->send_queue)){
		queue->working = true;
		uv_async_send(&queue->ping);
	}
}

/* secondary thread */
static void
write_ping(uv_async_t *handle, int status)
{
	async_io_t *async_io = (async_io_t *)handle->data;
	async_io_worker_poll_start(async_io);
}

static void
async_io_worker_write_callback(uv_work_t *req, int status)
{
	async_io_baton_t *baton = (async_io_baton_t*)req->data;
	void *data = baton->async_io_queue->data;
	async_io_queue_t *async_io_queue = baton->async_io_queue;

	ngx_queue_t *q;
	async_io_buf_t *buf;

	while(!ngx_queue_empty(&async_io_queue->work_done)) {
		q = ngx_queue_head(&async_io_queue->work_done);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
		if (async_io_queue->_done)
			async_io_queue->_done(data, buf);
		ngx_queue_remove(q);
		ngx_queue_insert_tail(&async_io_queue->avail_queue, q);
	}

	if (async_io_queue->_cb)
		async_io_queue->_cb(data, status);

	async_io_queue->working = 0;
	if (!ngx_queue_empty(&async_io_queue->ready_queue))
		uv_async_send(&async_io_queue->ping);
}

/* main thread */
static void
write_pong(uv_async_t *handle, int status)
{
	async_io_t *async_io = (async_io_t *)handle->data;

	if (async_io->write_io.working)
		return;

	async_io->write_io.working = 1;
	async_io_baton_t *baton = &async_io->write_io.baton;
	baton->async_io_queue = &async_io->write_io;
	baton->async_io = async_io;
	baton->events = UV_WRITABLE;
	baton->req.data = (void *)baton;
	ngx_empty_into(&async_io->write_io.ready_queue, &async_io->write_io.work_queue,32);
	uv_queue_work(server.loop, &baton->req, async_io_do, async_io_worker_write_callback);
}

static void
async_io_worker_do(async_io_queue_t *async_io_queue)
{
	void *data = async_io_queue->data;
	ngx_queue_t *q;
	async_io_buf_t *buf;

	while(!ngx_queue_empty(&async_io_queue->work_queue)) {
		q = ngx_queue_head(&async_io_queue->work_queue);
		buf = ngx_queue_data(q, async_io_buf_t, queue);
		if (!async_io_queue->_each(data, buf)) {
			break;
		}
		ngx_queue_remove(q);
		ngx_queue_insert_tail(&async_io_queue->work_done, q);
	}
}

static int
async_io_worker_poll_start(async_io_t *async_io)
{
	int retval = UV_OK;
	int flags = 0;
	if (!ngx_queue_empty(&async_io->read_io.avail_queue)) {
		flags |= UV_READABLE;
	}
	if (!async_io->write_io.working && !ngx_queue_empty(&async_io->write_io.ready_queue)) {
		flags |= UV_WRITABLE;
	}

	if (flags) {
		retval = uv_poll_start(&async_io->poll, flags, async_io_worker_poll_cb);
	} else {
		async_io_poll_stop(async_io);
	}

	return retval;

}

static void
async_io_worker_poll_cb(uv_poll_t *poll, int status, int events)
{
	async_io_t *async_io = (async_io_t *)poll->data;
	if(status == -1){
		uv_err_t err = uv_last_error(async_io->worker.loop);
		red_log(REDD_DEBUG, "hey we got an error!! %s",uv_strerror(err));
	}

	if (events & UV_READABLE) {
		ngx_empty_into(&async_io->read_io.avail_queue, &async_io->read_io.work_queue,-1);
		async_io_worker_do(&async_io->read_io);
		send_read_buffers(async_io);
	}
	if ((events & UV_WRITABLE) && !async_io->write_io.working) {
		uv_async_send(&async_io->write_io.pong);
	}
	async_io_worker_poll_start(async_io);
}


static void
init_worker(void *data)
{
	async_io_t *async_io = (async_io_t *)data;
	async_io_worker_poll_start(async_io);
	uv_run(async_io->worker.loop, UV_RUN_DEFAULT);
}

static int
worker_start(async_io_t *async_io)
{
	worker_thread_t *worker = &async_io->worker;

	uv_async_init(worker->loop,&async_io->write_io.ping,write_ping);
	uv_async_init(server.loop,&async_io->write_io.pong,write_pong);
	uv_async_init(server.loop,&async_io->read_io.ping,read_ping);
	uv_async_init(worker->loop,&async_io->read_io.pong,read_pong);

	async_io->write_io.ping.data = async_io;
	async_io->write_io.pong.data = async_io;
	async_io->read_io.ping.data = async_io;
	async_io->read_io.pong.data = async_io;

	async_io->write_io.working = false;
	async_io->read_io.working = false;

	ngx_queue_init(&async_io->write_io.send_queue);
	ngx_queue_init(&async_io->read_io.send_queue);
	return uv_thread_create(&worker->me, init_worker, async_io);
}


int async_io_worker_init(async_io_t *async_io, int fd, void *data,
	int read_buf_len, int read_buf_count,
	async_io_each read_each, async_io_done read_done, async_io_cb read_cb,
	int write_buf_len, int write_buf_count,
	async_io_each write_each, async_io_done write_done, async_io_cb write_cb)
{
	async_io->fd = fd;
	async_io->worker.loop = uv_loop_new();
	async_io_queue_init(&async_io->read_io, fd, UV_READABLE, data,
		read_buf_len, read_buf_count,
		read_each, read_done, read_cb);
	async_io_queue_init(&async_io->write_io, fd, UV_WRITABLE, data,
		write_buf_len, write_buf_count,
		write_each, write_done, write_cb);

	return async_io_poll_init(async_io,async_io->worker.loop,worker_start);
}

static void
async_io_buf_free(async_io_buf_t *buf)
{
	if (buf->buf)
		free(buf->buf);
	free(buf);
}

static void
async_io_queue_shutdown(async_io_queue_t *async_io_queue)
{
	while(!ngx_queue_empty(&async_io_queue->avail_queue)) {
		ngx_queue_t *q;
		q = ngx_queue_last(&async_io_queue->avail_queue);
		ngx_queue_remove(q);
		async_io_buf_free(ngx_queue_data(q, async_io_buf_t, queue));
	}
	while(!ngx_queue_empty(&async_io_queue->ready_queue)) {
		ngx_queue_t *q;
		q = ngx_queue_last(&async_io_queue->ready_queue);
		ngx_queue_remove(q);
		async_io_buf_free(ngx_queue_data(q, async_io_buf_t, queue));
	}
	while(!ngx_queue_empty(&async_io_queue->work_queue)) {
		ngx_queue_t *q;
		q = ngx_queue_last(&async_io_queue->work_queue);
		ngx_queue_remove(q);
		async_io_buf_free(ngx_queue_data(q, async_io_buf_t, queue));
	}
	while(!ngx_queue_empty(&async_io_queue->work_done)) {
		ngx_queue_t *q;
		q = ngx_queue_last(&async_io_queue->work_done);
		ngx_queue_remove(q);
		async_io_buf_free(ngx_queue_data(q, async_io_buf_t, queue));
	}

	ngx_queue_init(&async_io_queue->avail_queue);
	ngx_queue_init(&async_io_queue->ready_queue);
	ngx_queue_init(&async_io_queue->work_queue);
	ngx_queue_init(&async_io_queue->work_done);
}

void
async_io_shutdown(async_io_t *async_io)
{
	async_io_poll_stop(async_io);
	async_io_queue_shutdown(&async_io->read_io);
	async_io_queue_shutdown(&async_io->write_io);
}
