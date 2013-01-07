
/*
 * Copyright (C) Yichun Zhang (agentzh)
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef DDEBUG
#define DDEBUG 0
#endif
#include "ddebug.h"


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <sregex/sregex.h>


enum {
    SREGEX_COMPILER_POOL_SIZE = 4096
};


typedef struct {
    sre_pool_t              *compiler_pool;
} ngx_http_replace_main_conf_t;


typedef struct {
    ngx_str_t                  match;
    ngx_str_t                  value;

    ngx_flag_t                 once;
    sre_uint_t                 ncaps;
    size_t                     ovecsize;
    sre_program_t             *program;

    ngx_hash_t                 types;
    ngx_array_t               *types_keys;
} ngx_http_replace_loc_conf_t;


typedef struct {
    sre_int_t                  stream_pos;
    sre_int_t                 *ovector;
    sre_pool_t                *vm_pool;
    sre_vm_pike_ctx_t         *vm_ctx;

    ngx_chain_t               *pending; /* pending data before the
                                           pending matched capture */
    ngx_chain_t              **last_pending;

    ngx_chain_t               *pending2; /* pending data after the pending
                                            matched capture */
    ngx_chain_t              **last_pending2;

    ngx_buf_t                 *buf;

    u_char                    *pos;
    u_char                    *copy_start;
    u_char                    *copy_end;

    ngx_chain_t               *in;
    ngx_chain_t               *out;
    ngx_chain_t              **last_out;
    ngx_chain_t               *busy;
    ngx_chain_t               *free;
    ngx_chain_t               *special;
    ngx_chain_t              **last_special;
    ngx_chain_t               *rematch;

    size_t                     total_buffered;

    unsigned                   once:1;
    unsigned                   vm_done:1;
    unsigned                   special_buf:1;
    unsigned                   last_buf:1;
} ngx_http_replace_ctx_t;


static ngx_int_t ngx_http_replace_output(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx);
static ngx_int_t ngx_http_replace_parse(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx, ngx_chain_t *rematch);

static char * ngx_http_replace_filter(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);
static void *ngx_http_replace_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_replace_merge_loc_conf(ngx_conf_t *cf,
    void *parent, void *child);
static ngx_int_t ngx_http_replace_filter_init(ngx_conf_t *cf);
void ngx_http_replace_cleanup_pool(void *data);
static ngx_chain_t * ngx_http_replace_get_free_buf(ngx_pool_t *p,
    ngx_chain_t **free);
static void * ngx_http_replace_create_main_conf(ngx_conf_t *cf);
static ngx_chain_t *
    ngx_http_replace_new_pending_buf(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx, sre_int_t from, sre_int_t to);
static ngx_int_t ngx_http_replace_split_chain(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx, ngx_chain_t **pa, ngx_chain_t ***plast_a,
    sre_int_t split, ngx_chain_t **pb, ngx_chain_t ***plast_b, unsigned b_sane);
#if (DDEBUG)
static void ngx_http_replace_dump_chain(const char *prefix, ngx_chain_t **pcl,
    ngx_chain_t **last);
#endif
static void ngx_http_replace_check_total_buffered(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx, sre_int_t len, sre_int_t mlen);


static ngx_command_t  ngx_http_replace_filter_commands[] = {

    { ngx_string("replace_filter"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_TAKE23,
      ngx_http_replace_filter,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("replace_filter_types"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_1MORE,
      ngx_http_types_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_replace_loc_conf_t, types_keys),
      &ngx_http_html_default_types[0] },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_replace_filter_module_ctx = {
    NULL,                                  /* preconfiguration */
    ngx_http_replace_filter_init,          /* postconfiguration */

    ngx_http_replace_create_main_conf,     /* create main configuration */
    NULL,                                  /* init main configuration */

    NULL,                                  /* create server configuration */
    NULL,                                  /* merge server configuration */

    ngx_http_replace_create_loc_conf,      /* create location configuration */
    ngx_http_replace_merge_loc_conf        /* merge location configuration */
};


ngx_module_t  ngx_http_replace_filter_module = {
    NGX_MODULE_V1,
    &ngx_http_replace_filter_module_ctx,   /* module context */
    ngx_http_replace_filter_commands,      /* module directives */
    NGX_HTTP_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;
static ngx_http_output_body_filter_pt    ngx_http_next_body_filter;


static ngx_int_t
ngx_http_replace_header_filter(ngx_http_request_t *r)
{
    ngx_pool_cleanup_t            *cln;
    ngx_http_replace_ctx_t        *ctx;
    ngx_http_replace_loc_conf_t  *rlcf;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_http_replace_filter_module);

    if (rlcf->match.len == 0
        || r->headers_out.content_length_n == 0
        || ngx_http_test_content_type(r, &rlcf->types) == NULL)
    {
        return ngx_http_next_header_filter(r);
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_replace_ctx_t));
    if (ctx == NULL) {
        return NGX_ERROR;
    }

    ctx->last_special = &ctx->special;
    ctx->last_pending = &ctx->pending;
    ctx->last_pending2 = &ctx->pending2;

    ctx->ovector = ngx_palloc(r->pool, rlcf->ovecsize);
    if (ctx->ovector == NULL) {
        return NGX_ERROR;
    }

    ctx->vm_pool = sre_create_pool(1024);
    if (ctx->vm_pool == NULL) {
        return NGX_ERROR;
    }

    dd("created vm pool %p", ctx->vm_pool);

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        sre_destroy_pool(ctx->vm_pool);
        return NGX_ERROR;
    }

    cln->data = ctx->vm_pool;
    cln->handler = ngx_http_replace_cleanup_pool;

    ctx->vm_ctx = sre_vm_pike_create_ctx(ctx->vm_pool, rlcf->program,
                                         ctx->ovector, rlcf->ovecsize);
    if (ctx->vm_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_replace_filter_module);

    ctx->last_out = &ctx->out;

    r->filter_need_in_memory = 1;

    if (r == r->main) {
        ngx_http_clear_content_length(r);
        ngx_http_clear_last_modified(r);
    }

    return ngx_http_next_header_filter(r);
}


void
ngx_http_replace_cleanup_pool(void *data)
{
    sre_pool_t          *pool = data;

    if (pool) {
        dd("destroy sre pool %p", pool);
        sre_destroy_pool(pool);
    }
}


static ngx_int_t
ngx_http_replace_body_filter(ngx_http_request_t *r, ngx_chain_t *in)
{
    ngx_int_t                  rc;
    ngx_buf_t                 *b;
    ngx_chain_t               *cl, *cur = NULL, *rematch = NULL;

    ngx_http_replace_ctx_t        *ctx;
    ngx_http_replace_loc_conf_t   *rlcf;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_http_replace_filter_module);

    ctx = ngx_http_get_module_ctx(r, ngx_http_replace_filter_module);

    if (ctx == NULL) {
        return ngx_http_next_body_filter(r, in);
    }

    if ((in == NULL
         && ctx->buf == NULL
         && ctx->in == NULL
         && ctx->busy == NULL))
    {
        return ngx_http_next_body_filter(r, in);
    }

    if ((ctx->once || ctx->vm_done) && (ctx->buf == NULL || ctx->in == NULL)) {

        if (ctx->busy) {
            if (ngx_http_replace_output(r, ctx) == NGX_ERROR) {
                return NGX_ERROR;
            }
        }

        return ngx_http_next_body_filter(r, in);
    }

    /* add the incoming chain to the chain ctx->in */

    if (in) {
        if (ngx_chain_add_copy(r->pool, &ctx->in, in) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "http sub filter \"%V\"", &r->uri);

    while (ctx->in || ctx->buf) {

        if (ctx->buf == NULL) {
            cur = ctx->in;
            ctx->buf = cur->buf;
            ctx->in = cur->next;

            ctx->pos = ctx->buf->pos;
            ctx->special_buf = ngx_buf_special(ctx->buf);
            ctx->last_buf = (ctx->buf->last_buf || ctx->buf->last_in_chain);

            dd("=== new incoming buf: size=%d, special=%u, last=%u",
               (int) ngx_buf_size(ctx->buf), ctx->special_buf,
               ctx->last_buf);
        }

        b = NULL;

        while (ctx->pos < ctx->buf->last
               || (ctx->special_buf && ctx->last_buf))
        {
            rc = ngx_http_replace_parse(r, ctx, rematch);

            ngx_log_debug3(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                           "replace filter parse: %d, %p-%p",
                           rc, ctx->copy_start, ctx->copy_end);

            if (rc == NGX_ERROR) {
                return rc;
            }

            if (rc == NGX_DECLINED) {

                if (ctx->pending) {
                    *ctx->last_out = ctx->pending;
                    ctx->last_out = ctx->last_pending;

                    ctx->pending = NULL;
                    ctx->last_pending = &ctx->pending;
                }

                if (!ctx->special_buf) {
                    ctx->copy_start = ctx->pos;
                    ctx->copy_end = ctx->buf->last;
                    ctx->pos = ctx->buf->last;

                } else {
                    ctx->copy_start = NULL;
                    ctx->copy_end = NULL;
                }

                sre_reset_pool(ctx->vm_pool);
                ctx->vm_done = 1;
            }

            dd("copy_end - copy_start: %d, special: %u",
               (int) (ctx->copy_end - ctx->copy_start), ctx->special_buf);

            if (ctx->copy_start != ctx->copy_end && !ctx->special_buf) {
                dd("copy: %.*s", (int) (ctx->copy_end - ctx->copy_start),
                   ctx->copy_start);

                cl = ngx_http_replace_get_free_buf(r->pool, &ctx->free);
                if (cl == NULL) {
                    return NGX_ERROR;
                }

                b = cl->buf;

                b->memory = 1;
                b->pos = ctx->copy_start;
                b->last = ctx->copy_end;

                *ctx->last_out = cl;
                ctx->last_out = &cl->next;
            }

            if (rc == NGX_AGAIN) {
                if (ctx->special_buf && ctx->last_buf) {
                    break;
                }

                continue;
            }

            if (rc == NGX_DECLINED) {
                break;
            }

            /* rc == NGX_OK || rc == NGX_BUSY */

            cl = ngx_http_replace_get_free_buf(r->pool, &ctx->free);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            b = cl->buf;

            dd("free data buf: %p", b);

            dd("emit replaced value: \"%.*s\"", (int) rlcf->value.len,
               rlcf->value.data);

            if (rlcf->value.len) {
                b->memory = 1;
                b->pos = rlcf->value.data;
                b->last = rlcf->value.data + rlcf->value.len;

            } else {
                b->sync = 1;
            }

            cl->buf = b;
            cl->next = NULL;

            *ctx->last_out = cl;
            ctx->last_out = &cl->next;

            ctx->once = rlcf->once;

            if (rc == NGX_BUSY) {
                dd("goto rematch");
                goto rematch;
            }

            if (ctx->special_buf) {
                break;
            }

            continue;
        }

        if ((ctx->buf->flush || ctx->last_buf || ngx_buf_in_memory(ctx->buf))
            && cur)
        {
            if (b == NULL) {
                cl = ngx_http_replace_get_free_buf(r->pool, &ctx->free);
                if (cl == NULL) {
                    return NGX_ERROR;
                }

                b = cl->buf;
                b->sync = 1;

                *ctx->last_out = cl;
                ctx->last_out = &cl->next;
            }

            dd("setting shadow and last buf: %d", (int) ctx->buf->last_buf);
            b->last_buf = ctx->buf->last_buf;
            b->last_in_chain = ctx->buf->last_in_chain;
            b->flush = ctx->buf->flush;
            b->shadow = ctx->buf;
            b->recycled = ctx->buf->recycled;
        }

        if (!ctx->special_buf) {
            ctx->stream_pos += ctx->buf->last - ctx->buf->pos;
        }

        if (rematch) {
            rematch->next = ctx->free;
            ctx->free = rematch;
            rematch = NULL;
        }

rematch:
        dd("ctx->rematch: %p", ctx->rematch);

        if (ctx->rematch == NULL) {
            ctx->buf = NULL;
            cur = NULL;

        } else {

            if (cur) {
                ctx->in = cur;
                cur = NULL;
            }

            ctx->buf = ctx->rematch->buf;

            dd("ctx->buf set to rematch buf %p, len=%d, next=%p",
               ctx->buf, (int) ngx_buf_size(ctx->buf), ctx->rematch->next);

            rematch = ctx->rematch;
            ctx->rematch = rematch->next;

            ctx->pos = ctx->buf->pos;
            ctx->special_buf = ngx_buf_special(ctx->buf);
            ctx->last_buf = (ctx->buf->last_buf || ctx->buf->last_in_chain);
            ctx->stream_pos = ctx->buf->file_pos;
        }

#if (DDEBUG)
        ngx_http_replace_dump_chain("ctx->pending", &ctx->pending,
                                    ctx->last_pending);
        ngx_http_replace_dump_chain("ctx->pending2", &ctx->pending2,
                                    ctx->last_pending2);
#endif
    } /* while */

    if (ctx->out == NULL && ctx->busy == NULL) {
        return NGX_OK;
    }

    return ngx_http_replace_output(r, ctx);
}


static ngx_int_t
ngx_http_replace_output(ngx_http_request_t *r, ngx_http_replace_ctx_t *ctx)
{
    ngx_int_t     rc;
    ngx_buf_t    *b;
    ngx_chain_t  *cl;

#if (DDEBUG)
    b = NULL;
    for (cl = ctx->out; cl; cl = cl->next) {
        ngx_log_debug2(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                       "replace out: %p %p", cl->buf, cl->buf->pos);
        if (cl->buf == b) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                          "the same buf was used in sub");
            ngx_debug_point();
            return NGX_ERROR;
        }
        b = cl->buf;
    }

    ngx_http_replace_dump_chain("ctx->out", &ctx->out, ctx->last_out);
#endif

    rc = ngx_http_next_body_filter(r, ctx->out);

    if (ctx->busy == NULL) {
        ctx->busy = ctx->out;

    } else {
        for (cl = ctx->busy; cl->next; cl = cl->next) { /* void */ }
        cl->next = ctx->out;
    }

    ctx->out = NULL;
    ctx->last_out = &ctx->out;

    while (ctx->busy) {

        cl = ctx->busy;
        b = cl->buf;

        if (ngx_buf_size(b) != 0) {
            break;
        }

        if (cl->buf->tag != (ngx_buf_tag_t) &ngx_http_replace_filter_module) {
            ctx->busy = cl->next;
            ngx_free_chain(r->pool, cl);
            continue;
        }

        if (b->shadow) {
            b->shadow->pos = b->shadow->last;
            b->shadow->file_pos = b->shadow->file_last;
        }

        ctx->busy = cl->next;

        if (ngx_buf_special(b)) {

            /* collect special bufs to ctx->special, which may still be busy */

            cl->next = NULL;
            *ctx->last_special = cl;
            ctx->last_special = &cl->next;

        } else {

            /* add ctx->special to ctx->free because they cannot be busy at
             * this point */

            *ctx->last_special = ctx->free;
            ctx->free = ctx->special;
            ctx->special = NULL;
            ctx->last_special = &ctx->special;

#if 0
            /* free the temporary buf's data block if it is big enough */
            if (b->temporary
                && b->start != NULL
                && b->end - b->start > (ssize_t) r->pool->max)
            {
                ngx_pfree(r->pool, b->start);
            }
#endif

            /* add the data buf itself to the free buf chain */

            cl->next = ctx->free;
            ctx->free = cl;
        }
    }

    if (ctx->in || ctx->buf) {
        r->buffered |= NGX_HTTP_SUB_BUFFERED;

    } else {
        r->buffered &= ~NGX_HTTP_SUB_BUFFERED;
    }

    return rc;
}


static ngx_int_t
ngx_http_replace_parse(ngx_http_request_t *r, ngx_http_replace_ctx_t *ctx,
    ngx_chain_t *rematch)
{
    sre_int_t              rc, from, to, mfrom = -1, mto = -1;
    ngx_chain_t           *new_rematch = NULL;
    ngx_chain_t           *cl;
    ngx_chain_t          **last_rematch, **last;
    size_t                 len;
    sre_int_t             *pending_matched;

    if (ctx->once || ctx->vm_done) {
        ctx->copy_start = ctx->pos;
        ctx->copy_end = ctx->buf->last;
        ctx->pos = ctx->buf->last;

        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0, "once");

        return NGX_AGAIN;
    }

    len = ctx->buf->last - ctx->pos;

    dd("=== process data chunk %p len=%d, pos=%u, special=%u, "
       "last=%u, \"%.*s\"", ctx->buf, (int) (ctx->buf->last - ctx->pos),
       (int) (ctx->pos - ctx->buf->pos + ctx->stream_pos),
       ctx->special_buf, ctx->last_buf,
       (int) (ctx->buf->last - ctx->pos), ctx->pos);

    rc = sre_vm_pike_exec(ctx->vm_ctx, ctx->pos, len, ctx->last_buf,
                          &pending_matched);

    dd("vm pike exec: %d", (int) rc);

    switch (rc) {
    case SRE_OK:
        ctx->total_buffered = 0;

        from = ctx->ovector[0];
        to = ctx->ovector[1];

        dd("pike vm ok: (%d, %d)", (int) from, (int) to);

        new_rematch = 0;

        if (from >= ctx->stream_pos) {
            /* the match is completely on the current buf */

            if (ctx->pending) {
                *ctx->last_out = ctx->pending;
                ctx->last_out = ctx->last_pending;

                ctx->pending = NULL;
                ctx->last_pending = &ctx->pending;
            }

            if (ctx->pending2) {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                              "assertion failed: ctx->pending2 is not NULL "
                              "when the match is completely on the current "
                              "buf");
                return NGX_ERROR;
            }

            ctx->copy_start = ctx->pos;
            ctx->copy_end = ctx->buf->pos + (from - ctx->stream_pos);

            dd("copy len: %d", (int) (ctx->copy_end - ctx->copy_start));

            ctx->pos = ctx->buf->pos + (to - ctx->stream_pos);
            return NGX_OK;
        }

        /* from < ctx->stream_pos */

        if (ctx->pending) {

            if (ngx_http_replace_split_chain(r, ctx, &ctx->pending,
                                             &ctx->last_pending, from,
                                             &cl, &last, 0)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            if (ctx->pending) {
                *ctx->last_out = ctx->pending;
                ctx->last_out = ctx->last_pending;

                ctx->pending = NULL;
                ctx->last_pending = &ctx->pending;
            }

            if (cl) {
                *last = ctx->free;
                ctx->free = cl;
            }
        }

        if (ctx->pending2) {

            if (ngx_http_replace_split_chain(r, ctx, &ctx->pending2,
                                             &ctx->last_pending2,
                                             to, &new_rematch, &last_rematch, 1)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            if (ctx->pending2) {
                *ctx->last_pending2 = ctx->free;
                ctx->free = ctx->pending2;

                ctx->pending2 = NULL;
                ctx->last_pending2 = &ctx->pending2;
            }

            if (new_rematch) {
                if (rematch) {
                    ctx->rematch = rematch;
                }

                /* prepend cl to ctx->rematch */
                *last_rematch = ctx->rematch;
                ctx->rematch = new_rematch;
            }
        }

#if (DDEBUG)
        ngx_http_replace_dump_chain("ctx->rematch", &ctx->rematch, NULL);
#endif

        ctx->copy_start = NULL;
        ctx->copy_end = NULL;

        ctx->pos = ctx->buf->pos + (to - ctx->stream_pos);

        return new_rematch ? NGX_BUSY : NGX_OK;

    case SRE_AGAIN:
        from = ctx->ovector[0];
        to = ctx->ovector[1];

        dd("pike vm again: (%d, %d)", (int) from, (int) to);

        if (from == -1) {
            from = ctx->stream_pos + (ctx->buf->last - ctx->buf->pos);
        }

        if (to == -1) {
            to = ctx->stream_pos + (ctx->buf->last - ctx->buf->pos);
        }

        dd("pike vm again (adjusted): stream pos:%d, (%d, %d)",
           (int) ctx->stream_pos, (int) from, (int) to);

        if (from > to) {
            ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                          "invalid capture range: %i > %i", (ngx_int_t) from,
                          (ngx_int_t) to);
            return NGX_ERROR;
        }

        if (pending_matched) {
            mfrom = pending_matched[0];
            mto = pending_matched[1];

            dd("pending matched: (%ld, %ld)", (long) mfrom, (long) mto);
        }

        if (from == to) {
            if (ctx->pending) {
                ctx->total_buffered = 0;

                dd("output pending");
                *ctx->last_out = ctx->pending;
                ctx->last_out = ctx->last_pending;

                ctx->pending = NULL;
                ctx->last_pending = &ctx->pending;
            }

            ctx->copy_start = ctx->pos;
            ctx->copy_end = ctx->buf->pos + (from - ctx->stream_pos);
            ctx->pos = ctx->copy_end;

            ngx_http_replace_check_total_buffered(r, ctx, to - from,
                                                  mto - mfrom);
            return NGX_AGAIN;
        }

        /*
         * append the existing ctx->pending data right before
         * the $& capture to ctx->out.
         */

        if (from >= ctx->stream_pos) {
            /* the match is completely on the current buf */

            ctx->copy_start = ctx->pos;
            ctx->copy_end = ctx->buf->pos + (from - ctx->stream_pos);

            if (ctx->pending) {
                ctx->total_buffered = 0;

                *ctx->last_out = ctx->pending;
                ctx->last_out = ctx->last_pending;

                ctx->pending = NULL;
                ctx->last_pending = &ctx->pending;
            }

            if (ctx->pending2) {
                ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                              "assertion failed: ctx->pending2 is not NULL "
                              "when the match is completely on the current "
                              "buf");
                return NGX_ERROR;
            }

            if (pending_matched) {

                if (from < mfrom) {
                    /* create ctx->pending as (from, mfrom) */

                    cl = ngx_http_replace_new_pending_buf(r, ctx, from, mfrom);
                    if (cl == NULL) {
                        return NGX_ERROR;
                    }

                    *ctx->last_pending = cl;
                    ctx->last_pending = &cl->next;
                }

                if (mto < to) {
                    /* create ctx->pending2 as (mto, to) */
                    cl = ngx_http_replace_new_pending_buf(r, ctx, mto, to);
                    if (cl == NULL) {
                        return NGX_ERROR;
                    }

                    *ctx->last_pending2 = cl;
                    ctx->last_pending2 = &cl->next;
                }

            } else {
                dd("create ctx->pending as (%ld, %ld)", (long) from, (long) to);
                cl = ngx_http_replace_new_pending_buf(r, ctx, from, to);
                if (cl == NULL) {
                    return NGX_ERROR;
                }

                *ctx->last_pending = cl;
                ctx->last_pending = &cl->next;
            }

            ctx->pos = ctx->buf->last;

            ngx_http_replace_check_total_buffered(r, ctx, to - from,
                                                  mto - mfrom);

            return NGX_AGAIN;
        }

        dd("from < ctx->stream_pos");

        if (ctx->pending) {
            /* split ctx->pending into ctx->out and ctx->pending */

            if (ngx_http_replace_split_chain(r, ctx, &ctx->pending,
                                             &ctx->last_pending, from, &cl,
                                             &last, 1)
                != NGX_OK)
            {
                return NGX_ERROR;
            }

            if (ctx->pending) {
                dd("adjust pending: pos=%d, from=%d",
                   (int) ctx->pending->buf->file_pos, (int) from);

                ctx->total_buffered -= (size_t)
                    (from - ctx->pending->buf->file_pos);

                *ctx->last_out = ctx->pending;
                ctx->last_out = ctx->last_pending;

                ctx->pending = NULL;
                ctx->last_pending = &ctx->pending;
            }

            if (cl) {
                dd("splitted ctx->pending into ctx->out and ctx->pending: %d",
                   (int) ctx->total_buffered);
                ctx->pending = cl;
                ctx->last_pending = last;
            }

            if (pending_matched && !ctx->pending2 && mto >= ctx->stream_pos) {
                dd("splitting ctx->pending into ctx->pending and ctx->free");

                if (ngx_http_replace_split_chain(r, ctx, &ctx->pending,
                                                 &ctx->last_pending, mfrom, &cl,
                                                 &last, 0)
                    != NGX_OK)
                {
                    return NGX_ERROR;
                }

                if (cl) {
                    ctx->total_buffered -= (size_t) (ctx->stream_pos - mfrom);

                    dd("splitted ctx->pending into ctx->pending and ctx->free");
                    *last = ctx->free;
                    ctx->free = cl;
                }
            }
        }

        if (ctx->pending2) {

            if (pending_matched) {
                dd("splitting ctx->pending2 into ctx->free and ctx->pending2");

                if (ngx_http_replace_split_chain(r, ctx, &ctx->pending2,
                                                 &ctx->last_pending2,
                                                 mto, &cl, &last, 1)
                    != NGX_OK)
                {
                    return NGX_ERROR;
                }

                if (ctx->pending2) {

                    dd("total buffered reduced by %d (was %d)",
                       (int) (mto - ctx->pending2->buf->file_pos),
                       (int) ctx->total_buffered);

                    ctx->total_buffered -= (size_t)
                        (mto - ctx->pending2->buf->file_pos);

                    *ctx->last_pending2 = ctx->free;
                    ctx->free = ctx->pending2;

                    ctx->pending2 = NULL;
                    ctx->last_pending2 = &ctx->pending2;
                }

                if (cl) {
                    ctx->pending2 = cl;
                    ctx->last_pending2 = last;
                }
            }

            if (mto < to) {
                dd("new pending data to buffer to ctx->pending2: (%ld, %ld)",
                   (long) mto, (long) to);

                cl = ngx_http_replace_new_pending_buf(r, ctx, mto, to);
                if (cl == NULL) {
                    return NGX_ERROR;
                }

                *ctx->last_pending2 = cl;
                ctx->last_pending2 = &cl->next;
            }

            ctx->copy_start = NULL;
            ctx->copy_end = NULL;

            ctx->pos = ctx->buf->last;

            ngx_http_replace_check_total_buffered(r, ctx, to - from,
                                                  mto - mfrom);

            return NGX_AGAIN;
        }

        /* ctx->pending2 == NULL */

        if (pending_matched) {

            if (mto < to) {
                /* new pending data to buffer to ctx->pending2 */
                cl = ngx_http_replace_new_pending_buf(r, ctx, mto, to);
                if (cl == NULL) {
                    return NGX_ERROR;
                }

                *ctx->last_pending2 = cl;
                ctx->last_pending2 = &cl->next;
            }

            /* otherwise no new data to buffer */

        } else {

            /* new pending data to buffer to ctx->pending */
            cl = ngx_http_replace_new_pending_buf(r, ctx, ctx->pos
                                                  - ctx->buf->pos
                                                  + ctx->stream_pos, to);
            if (cl == NULL) {
                return NGX_ERROR;
            }

            *ctx->last_pending = cl;
            ctx->last_pending = &cl->next;
        }

        ctx->copy_start = NULL;
        ctx->copy_end = NULL;

        ctx->pos = ctx->buf->last;

        ngx_http_replace_check_total_buffered(r, ctx, to - from,
                                              mto - mfrom);

        return NGX_AGAIN;

    case SRE_DECLINED:
        ctx->total_buffered = 0;

        return NGX_DECLINED;

    default:
        /* SRE_ERROR */
        return NGX_ERROR;
    }

    /* cannot reach here */
}


static char *
ngx_http_replace_filter(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_replace_loc_conf_t     *rlcf = conf;
    ngx_http_replace_main_conf_t    *rmcf;

    int              flags = 0;
    sre_int_t        err_offset;
    ngx_str_t        prefix, suffix;
    u_char          *p;
    ngx_str_t       *value;
    ngx_uint_t       i;
    sre_pool_t      *ppool; /* parser pool */
    sre_regex_t     *re;
    sre_program_t   *prog;

    ngx_pool_cleanup_t              *cln;

    if (rlcf->match.len) {
        return "is duplicate";
    }

    value = cf->args->elts;

    rlcf->match = value[1];

    /* XXX check variable usage in the value */
    rlcf->value = value[2];

    rlcf->once = 1;  /* default to once */

    if (cf->args->nelts == 4) {
        /* 3 user args */

        p = value[3].data;

        for (i = 0; i < value[3].len; i++) {
            switch (p[i]) {
            case 'i':
                flags |= SRE_REGEX_CASELESS;
                break;

            case 'g':
                rlcf->once = 0;
                break;

            default:
                return "specifies an unrecognized regex flag";
            }
        }
    }

    ppool = sre_create_pool(1024);
    if (ppool == NULL) {
        return NGX_CONF_ERROR;
    }

    dd("regex: %s", value[1].data);

    re = sre_regex_parse(ppool, value[1].data, &rlcf->ncaps, flags,
                         &err_offset);
    if (re == NULL) {
        if (err_offset >= 0 && (size_t) err_offset <= rlcf->match.len) {
            prefix.data = rlcf->match.data;
            prefix.len = err_offset;

            suffix.data = rlcf->match.data + err_offset;
            suffix.len = rlcf->match.len - err_offset;

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "failed to parse regex at offset %i: "
                               "syntax error; marked by <-- HERE in "
                               "\"%V <-- HERE %V\"",
                               (ngx_int_t) err_offset, &prefix, &suffix);

        } else {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "failed to parse regex \"%V\"", &rlcf->match);
        }

        sre_destroy_pool(ppool);
        return NGX_CONF_ERROR;
    }

    rmcf =
        ngx_http_conf_get_module_main_conf(cf, ngx_http_replace_filter_module);

    if (rmcf->compiler_pool == NULL) {
        rmcf->compiler_pool = sre_create_pool(SREGEX_COMPILER_POOL_SIZE);
        if (rmcf->compiler_pool == NULL) {
            sre_destroy_pool(ppool);
            return NGX_CONF_ERROR;
        }

        cln = ngx_pool_cleanup_add(cf->pool, 0);
        if (cln == NULL) {
            sre_destroy_pool(rmcf->compiler_pool);
            rmcf->compiler_pool = NULL;
            sre_destroy_pool(ppool);
            return NGX_CONF_ERROR;
        }

        cln->data = rmcf->compiler_pool;
        cln->handler = ngx_http_replace_cleanup_pool;
    }

    prog = sre_regex_compile(rmcf->compiler_pool, re);

    sre_destroy_pool(ppool);

    if (prog == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "failed to compile regex \"%V\"", &rlcf->match);
        return NGX_CONF_ERROR;
    }

    rlcf->program = prog;
    rlcf->ovecsize = 2 * (rlcf->ncaps + 1) * sizeof(sre_int_t);

    return NGX_CONF_OK;
}


static void *
ngx_http_replace_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_replace_loc_conf_t  *rlcf;

    rlcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_replace_loc_conf_t));
    if (rlcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc():
     *
     *     conf->match = { 0, NULL };
     *     conf->value = { 0, NULL };
     *     conf->types = { NULL };
     *     conf->types_keys = NULL;
     *     conf->program = NULL;
     *     conf->ncaps = 0;
     *     conf->ovecsize = 0;
     */

    rlcf->once = NGX_CONF_UNSET;

    return rlcf;
}


static char *
ngx_http_replace_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_replace_loc_conf_t *prev = parent;
    ngx_http_replace_loc_conf_t *conf = child;

    ngx_conf_merge_value(conf->once, prev->once, 1);
    ngx_conf_merge_str_value(conf->match, prev->match, "");
    ngx_conf_merge_str_value(conf->value, prev->value, "");

    if (conf->program == NULL) {
        conf->program = prev->program;
        conf->ncaps = prev->ncaps;
        conf->ovecsize = prev->ovecsize;
    }

    if (ngx_http_merge_types(cf, &conf->types_keys, &conf->types,
                             &prev->types_keys, &prev->types,
                             ngx_http_html_default_types)
        != NGX_OK)
    {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_replace_filter_init(ngx_conf_t *cf)
{
    ngx_http_replace_main_conf_t    *rmcf;

    rmcf =
        ngx_http_conf_get_module_main_conf(cf, ngx_http_replace_filter_module);

    if (rmcf->compiler_pool != NULL) {
        ngx_http_next_header_filter = ngx_http_top_header_filter;
        ngx_http_top_header_filter = ngx_http_replace_header_filter;

        ngx_http_next_body_filter = ngx_http_top_body_filter;
        ngx_http_top_body_filter = ngx_http_replace_body_filter;
    }

    return NGX_OK;
}


static ngx_chain_t *
ngx_http_replace_get_free_buf(ngx_pool_t *p, ngx_chain_t **free)
{
    ngx_chain_t     *cl;

    cl = ngx_chain_get_free_buf(p, free);
    if (cl == NULL) {
        return cl;
    }

    ngx_memzero(cl->buf, sizeof(ngx_buf_t));

    cl->buf->tag = (ngx_buf_tag_t) &ngx_http_replace_filter_module;

    return cl;
}


static void *
ngx_http_replace_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_replace_main_conf_t    *rmcf;

    rmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_replace_main_conf_t));
    if (rmcf == NULL) {
        return NULL;
    }

    /* set by ngx_pcalloc:
     *      rmcf->compiler_pool = NULL;
     */

    return rmcf;
}


static ngx_int_t
ngx_http_replace_split_chain(ngx_http_request_t *r, ngx_http_replace_ctx_t *ctx,
    ngx_chain_t **pa, ngx_chain_t ***plast_a, sre_int_t split, ngx_chain_t **pb,
    ngx_chain_t ***plast_b, unsigned b_sane)
{
    sre_int_t            file_last;
    ngx_chain_t         *cl, *newcl, **ll;

#if 0
    b_sane = 0;
#endif

    ll = pa;
    for (cl = *pa; cl; ll = &cl->next, cl = cl->next) {
        if (cl->buf->file_last > split) {
            /* found an overlap */

            if (cl->buf->file_pos < split) {

                dd("adjust cl buf (b_sane=%d): \"%.*s\"", b_sane,
                   (int) ngx_buf_size(cl->buf), cl->buf->pos);

                file_last = cl->buf->file_last;
                cl->buf->last -= file_last - split;
                cl->buf->file_last = split;

                dd("adjusted cl buf (next=%p): %.*s",
                   cl->next,
                   (int) ngx_buf_size(cl->buf), cl->buf->pos);

                /* build the b chain */
                if (b_sane) {
                    newcl = ngx_http_replace_get_free_buf(r->pool,
                                                          &ctx->free);
                    if (newcl == NULL) {
                        return NGX_ERROR;
                    }

                    newcl->buf->memory = 1;
                    newcl->buf->pos = cl->buf->last;
                    newcl->buf->last = cl->buf->last + file_last - split;
                    newcl->buf->file_pos = split;
                    newcl->buf->file_last = file_last;

                    newcl->next = cl->next;

                    *pb = newcl;
                    if (plast_b) {
                        if (cl->next) {
                            *plast_b = *plast_a;

                        } else {
                            *plast_b = &newcl->next;
                        }
                    }

                } else {
                    *pb = cl->next;
                    if (plast_b) {
                        *plast_b = *plast_a;
                    }
                }

                /* truncate the a chain */
                *plast_a = &cl->next;
                cl->next = NULL;

                return NGX_OK;
            }

            /* build the b chain */
            *pb = cl;
            if (plast_b) {
                *plast_b = *plast_a;
            }

            /* truncate the a chain */
            *plast_a = ll;
            *ll = NULL;

            return NGX_OK;
        }
    }

    /* missed */

    *pb = NULL;
    if (plast_b) {
        *plast_b = pb;
    }

    return NGX_OK;
}


static ngx_chain_t *
ngx_http_replace_new_pending_buf(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx, sre_int_t from, sre_int_t to)
{
    size_t               len;
    ngx_buf_t           *b;
    ngx_chain_t         *cl;

    if (from < ctx->stream_pos) {
        from = ctx->stream_pos;
    }

    len = (size_t) (to - from);
    if (len == 0) {
        return NULL;
    }

    cl = ngx_http_replace_get_free_buf(r->pool, &ctx->free);
    if (cl == NULL) {
        return NULL;
    }

    b = cl->buf;
    b->temporary = 1;

    /* abuse the file_pos and file_last fields here */
    b->file_pos = from;
    b->file_last = to;

    b->start = ngx_palloc(r->pool, len);
    if (b->start == NULL) {
        return NULL;
    }
    b->end = b->start + len;

    b->pos = b->start;
    b->last = ngx_copy(b->pos, ctx->buf->pos + from - ctx->stream_pos, len);

    dd("buffered pending data: stream_pos=%ld (%ld, %ld): %.*s",
       (long) ctx->stream_pos, (long) from, (long) to,
       (int) len, ctx->buf->pos + from - ctx->stream_pos);

    ctx->total_buffered += len;

    return cl;
}


#if (DDEBUG)
static void
ngx_http_replace_dump_chain(const char *prefix, ngx_chain_t **pcl,
    ngx_chain_t **last)
{
    ngx_chain_t        *cl;

    if (*pcl == NULL) {
        dd("%s buf empty", prefix);
        if (last && last != pcl) {
            dd("BAD last %s", prefix);
            assert(0);
        }
    }

    for (cl = *pcl; cl; cl = cl->next) {
        dd("%s buf: \"%.*s\"", prefix, (int) ngx_buf_size(cl->buf),
           cl->buf->pos);

        if (cl->next == NULL) {
            if (last && last != &cl->next) {
                dd("BAD last %s", prefix);
                assert(0);
            }
        }
    }
}
#endif


static void
ngx_http_replace_check_total_buffered(ngx_http_request_t *r,
    ngx_http_replace_ctx_t *ctx, sre_int_t len, sre_int_t mlen)
{
    dd("total buffered: %d", (int) ctx->total_buffered);

    if ((ssize_t) ctx->total_buffered != len - mlen) {
        ngx_log_error(NGX_LOG_ALERT, r->connection->log, 0,
                      "replace filter: ctx->total_buffered out of "
                      "sync: it is %i but should be %uz",
                      ctx->total_buffered, (ngx_int_t) (len - mlen));

#if (DDEBUG)
        assert(0);
#endif
    }
}
