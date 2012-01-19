/*
** Copyright (C) Mellanox Technologies Ltd. 2001-2011.  ALL RIGHTS RESERVED.
**
** This software product is a proprietary product of Mellanox Technologies Ltd.
** (the "Company") and all right, title, and interest in and to the software product,
** including all associated intellectual property rights, are and shall
** remain exclusively with the Company.
**
** This software product is governed by the End User License Agreement
** provided with the software product.
**
*/

#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdlib.h>
#include <malloc.h>
#include <ctime>
#include <assert.h>
#include <math.h> //for sqrt

#include "reducer.h"
#include "InputClient.h"
#include "IOUtility.h"
#include "C2JNexus.h"

using namespace std;

extern merging_state_t merging_sm;
extern int num_stage_mem;
extern void *fetch_thread_main (void *context);
extern void *merge_thread_main (void *context);

static void init_reduce_task(struct reduce_task *task);

reduce_task_t * g_task;

void reduce_downcall_handler(const string & msg)
{
    client_part_req_t   *req;
    hadoop_cmd_t        *hadoop_cmd;

    hadoop_cmd = (hadoop_cmd_t*) malloc(sizeof(hadoop_cmd_t));
    memset(hadoop_cmd, 0, sizeof(hadoop_cmd_t));

    parse_hadoop_cmd(msg, *hadoop_cmd);

    log(lsDEBUG, "===>>> GOT COMMAND FROM JAVA SIDE (total %d params): hadoop_cmd->header=%d ", hadoop_cmd->count - 1, (int)hadoop_cmd->header);

    static const int DIRS_START = 4;
    switch (hadoop_cmd->header) {
    case INIT_MSG:
    	assert (hadoop_cmd->count -1 > 2); // sanity under debug
    	g_task->num_maps = atoi(hadoop_cmd->params[0]);
    	g_task->job_id = strdup(hadoop_cmd->params[1]);
    	g_task->reduce_task_id = strdup(hadoop_cmd->params[2]);
    	g_task->lpq_size = atoi(hadoop_cmd->params[3]);

    	if (hadoop_cmd->count -1  > DIRS_START) {
    		assert (hadoop_cmd->params[DIRS_START] != NULL); // sanity under debug
    		if (hadoop_cmd->params[DIRS_START] != NULL) {
    			int num_dirs = atoi(hadoop_cmd->params[DIRS_START]);
    			log(lsDEBUG, " ===>>> num_dirs=%d" , num_dirs);

    			assert (num_dirs >= 0); // sanity under debug
    			if (num_dirs > 0 && DIRS_START + 1 + num_dirs  <= hadoop_cmd->count - 1) {
    				g_task->local_dirs.resize(num_dirs);
    				for (int i = 0; i < num_dirs; ++i) {
    					g_task->local_dirs[i].assign(hadoop_cmd->params[DIRS_START + 1 + i]);
    					log(lsINFO, " -> dir[%d]=%s", i, g_task->local_dirs[i].c_str());
    				}
    			}
    		}
    	}
    	init_reduce_task(g_task);
    	free_hadoop_cmd(*hadoop_cmd);
    	free(hadoop_cmd);
    	break;

    	case FETCH_MSG:
            /*
        * 1. find the hostid
        * 2. map from the hostid to its request list
            * 3. lock the list and insert the new request
        */
            //string hostid = hadoop_cmd->params[0];

        /* map<string, host_list_t *>::iterator iter;
        host = NULL;
        bool is_new = false;

            pthread_mutex_lock(&g_task->lock);
            iter = g_task->hostmap->find(hostid);
            if (iter == g_task->hostmap->end()) {
            host = (host_list_t *) malloc(sizeof(host_list_t));
            pthread_mutex_init(&host->lock, NULL);
            INIT_LIST_HEAD(&host->todo_fetch_list);
            host->hostid = strdup(hostid.c_str());
                (*(g_task->hostmap))[hostid] = host;
            is_new = true;
        } else {
            host = iter->second;
        }
            pthread_mutex_unlock(&g_task->lock); */

        /* Insert a segment request into the list */
        req = (client_part_req_t *) malloc(sizeof(client_part_req_t));
        memset(req, 0, sizeof(client_part_req_t));
        req->info = hadoop_cmd;
        /* req->host = host; */
        req->total_len = 0;
        req->last_fetched = 0;
        req->mop = NULL;


            pthread_mutex_lock(&g_task->fetch_man->send_lock);
            g_task->fetch_man->fetch_list.push_back(req);
            pthread_cond_broadcast(&g_task->fetch_man->send_cond);
            pthread_mutex_unlock(&g_task->fetch_man->send_lock);

        /* pthread_mutex_lock(&host->lock);
        list_add_tail(&req->list, &host->todo_fetch_list);
        pthread_mutex_unlock(&host->lock);

            pthread_mutex_lock(&g_task->fetch_man->send_req_lock);
        if (is_new) {
                list_add_tail(&host->list, &g_task->fetch_man->send_req_list);
        }
            g_task->fetch_man->send_req_count++;
            pthread_mutex_unlock(&g_task->fetch_man->send_req_lock);*/

        /* wake up fetch thread */
            //pthread_cond_broadcast(&g_task->cond);

            write_log(g_task->reduce_log, DBG_CLIENT,
                      "Got 1 more fetch request, total is %d",
                      ++g_task->total_java_reqs);
            break;

    	case FINAL_MSG:
        /* do the final merge */
            pthread_mutex_lock(&g_task->merge_man->lock);
            g_task->merge_man->flag = FINAL_MERGE;
            pthread_cond_broadcast(&g_task->merge_man->cond);
            pthread_mutex_unlock(&g_task->merge_man->lock);
        free_hadoop_cmd(*hadoop_cmd);
        free(hadoop_cmd);
            break;

    	case EXIT_MSG:
            finalize_reduce_task(g_task);
        free_hadoop_cmd(*hadoop_cmd);
        free(hadoop_cmd);
            break;
    }

    log(lsDEBUG, "<<<=== HANDLED COMMAND FROM JAVA SIDE");
}

int  create_mem_pool(int size, int num, memory_pool_t *pool)
{
    int pagesize = getpagesize();
    uint64_t buf_len;
    int rc;

    pthread_mutex_init(&pool->lock, NULL);
    INIT_LIST_HEAD(&pool->free_descs);

    buf_len = size;
    pool->size = size;
    pool->num = num;
    pool->total_size = buf_len * num;

    log (lsDEBUG, "logsize is %d\n", size);
    log (lsDEBUG, "buf_len is %d\n", buf_len);
    log (lsDEBUG, "pool->total_size is %d\n", pool->total_size);
    
    rc = posix_memalign((void**)&pool->mem,  pagesize, pool->total_size);
    if (rc) {
    	output_stderr("unable to create pool. posix_memalign failed: alignment=%d , total_size=%ll --> rc=%d", pagesize, pool->total_size, rc );
        return -1;
    }

    log(lsDEBUG,"memalign successed - %lld bytes", pool->total_size);
    memset(pool->mem, 0, pool->total_size);

    for (int i = 0; i < num; ++i) {
        mem_desc_t *desc = (mem_desc_t *) malloc(sizeof(mem_desc_t));
        desc->buff  = pool->mem + i * buf_len;
        desc->buf_len = buf_len;
        desc->owner = pool;
        desc->status = INIT;
        pthread_mutex_init(&desc->lock, NULL);
        pthread_cond_init(&desc->cond, NULL);

        pthread_mutex_lock(&pool->lock);
        list_add_tail(&desc->list, &pool->free_descs);
        pthread_mutex_unlock(&pool->lock);
    }
    return 0;
}

static void init_reduce_task(struct reduce_task *task)
{
    /* Initialize log for reduce task */
    task->reduce_log = create_log(task->reduce_task_id);
    write_log(task->reduce_log, DBG_CLIENT, 
              "%s launched", 
              task->reduce_task_id); 

    write_log(task->reduce_log, DBG_CLIENT, 
             "Total Map is %d", 
             task->num_maps);     
    
    int num_lpqs;
    if (task->lpq_size > 0) {
    	num_lpqs = (task->num_maps / task->lpq_size);
    	// if more than one segment left then additional lpq added
    	// if only one segment left then the first will be larger
    	if ((task->num_maps % task->lpq_size) > 1) 
    		num_lpqs++;
    } else {
        num_lpqs = (int) sqrt(task->num_maps);
    }

    /* Initialize a merge manager thread */
    task->merge_man = new MergeManager(1, merging_sm.online, task, num_lpqs);

    memset(&task->merge_thread, 0, sizeof(netlev_thread_t));
    task->merge_thread.stop = 0;
    task->merge_thread.context = task;
    pthread_attr_init(&task->merge_thread.attr);
    pthread_attr_setdetachstate(&task->merge_thread.attr, 
                                PTHREAD_CREATE_JOINABLE); 
    log(lsINFO, "CREATING THREAD"); pthread_create(&task->merge_thread.thread,
                   &task->merge_thread.attr, 
                   merge_thread_main, task);

    /* Initialize a fetcher */
    task->fetch_man = new FetchManager(task);
    memset(&task->fetch_thread, 0, sizeof(netlev_thread_t));
    task->fetch_thread.stop = 0;
    task->fetch_thread.context = task;
    pthread_attr_init(&task->fetch_thread.attr); 
    pthread_attr_setdetachstate(&task->fetch_thread.attr, 
                                PTHREAD_CREATE_JOINABLE);
    log(lsINFO, "CREATING THREAD"); pthread_create(&task->fetch_thread.thread,
                   &task->fetch_thread.attr, 
                   fetch_thread_main, task);
}

void spawn_reduce_task()
{
    int netlev_kv_pool_size;

    g_task = (reduce_task_t *) malloc(sizeof(reduce_task_t));
    memset(g_task, 0, sizeof(*g_task));
    pthread_cond_init(&g_task->cond, NULL);
    pthread_mutex_init(&g_task->lock, NULL);

    g_task->mop_index = 0;

    /* init large memory pool for merged kv buffer */
    memset(&g_task->kv_pool, 0, sizeof(memory_pool_t));
    netlev_kv_pool_size  = 1 << NETLEV_KV_POOL_EXPO;
    if (create_mem_pool(netlev_kv_pool_size, num_stage_mem, &g_task->kv_pool)) {
    	log(lsFATAL, "failed to create memory pool for reduce g_task for merged kv buffer");
    	exit(-1);
    }

    /* report success spawn to java */
//    g_task->nexus->send_int((int)RT_LAUNCHED);
}



//------------------------------------------------------------------------------
void final_cleanup(){

	log(lsINFO, "-------------- STOPING PROCESS ---------");
    /* free map output pool */
    while (!list_empty(&merging_sm.mop_pool.free_descs)) {
        mem_desc_t *desc =
            list_entry(merging_sm.mop_pool.free_descs.next,
                       typeof(*desc), list);
        list_del(&desc->list);
        free(desc);
    }
    pthread_mutex_destroy(&merging_sm.mop_pool.lock);
    free(merging_sm.mop_pool.mem);
    log (lsDEBUG, "mop pool is freed");

    merging_sm.client->stop_client();
    log (lsDEBUG, "RDMA client is stoped");

    delete merging_sm.client;
    log (lsDEBUG, "RDMA client is deleted");

    log (lsDEBUG, "finished all C++ threads");

//    fclose(stdout);
    fclose(stderr);
}

//------------------------------------------------------------------------------
void finalize_reduce_task(reduce_task_t *task) 
{
   /* for measurement please enable the codes and set up your directory */
	log(lsINFO, "-------------- STOPING REDUCER ---------");

/*
 Avner: no one has ever updated this counters

    write_log(task->reduce_log, DBG_CLIENT, 
              "Total merge time: %d",  
              task->total_merge_time);
    write_log(task->reduce_log, DBG_CLIENT, 
              "Total upload time: %d", 
              task->total_upload_time);
    write_log(task->reduce_log, DBG_CLIENT, 
              "Total fetch time: %d",
              task->total_fetch_time);
//*/
    write_log(task->reduce_log, DBG_CLIENT,
              "Total wait  time: %d", 
              task->total_wait_mem_time);

    /* stop fetch thread */ 
    task->fetch_thread.stop = 1;
    pthread_cond_broadcast(&task->fetch_man->send_cond); // awake fetch thread for enabling him check fetch_thread.stop

    pthread_mutex_lock(&task->lock);
    pthread_cond_broadcast(&task->cond);
    pthread_mutex_unlock(&task->lock);
	log(lsDEBUG, ">> before joining fetch_thread");
    pthread_join(task->fetch_thread.thread, NULL); log(lsINFO, "THREAD JOINED");
	log(lsINFO, "-------------->>> fetch_thread has joined <<<<------------");
    delete task->fetch_man;
    
    /* stop merge thread - This will only happen after joining fetch_thread*/
    task->merge_thread.stop = 1;
    pthread_mutex_lock(&task->merge_man->lock);
    pthread_cond_broadcast(&task->merge_man->cond);
    pthread_mutex_unlock(&task->merge_man->lock);
	log(lsDEBUG, "<< before joining merge_thread");
    pthread_join(task->merge_thread.thread, NULL); log(lsINFO, "THREAD JOINED");
	log(lsINFO, "-------------->>> merge_thread has joined <<<<------------");
    delete task->merge_man;
   
    /* delete map */
    /* map <string, host_list_t*>::iterator iter =
        task->hostmap->begin();
    while (iter != task->hostmap->end()) {
        free((iter->second)->hostid);
        free(iter->second);
        iter++;
    }
    delete task->hostmap;
    DBGPRINT(DBG_CLIENT, "host lists and map are freed\n"); */

    /* free large pool */
	log(lsTRACE, ">> before free pool loop");
    while (!list_empty(&task->kv_pool.free_descs)) {
        mem_desc_t *desc = 
            list_entry(task->kv_pool.free_descs.next, 
                       typeof(*desc), list);
        list_del(&desc->list);
        free(desc);
    }
	log(lsTRACE, "<< after  free pool loop");
    pthread_mutex_destroy(&task->kv_pool.lock);
    free(task->kv_pool.mem);
    write_log(task->reduce_log, DBG_CLIENT, "kv pool is freed");

    pthread_mutex_destroy(&task->lock);
    pthread_cond_destroy(&task->cond);

    write_log(task->reduce_log, DBG_CLIENT, "reduce task is freed successfully");
    close_log(task->reduce_log);
    
    free(task->reduce_task_id);
    free(task->job_id);
    free(task);


    final_cleanup();

    log(lsTRACE, "*********  ALL C++ threads finished  ************");
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=4 sw=4 hlsearch cindent expandtab 
 */
