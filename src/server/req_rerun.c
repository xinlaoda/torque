/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/
/*
 * req_rerun.c - functions dealing with a Rerun Job Request
 */

#include <pbs_config.h>   /* the master config generated by configure */

#include <stdio.h>
#include "libpbs.h"
#include <signal.h>
#include <pthread.h>
#include "server_limits.h"
#include "list_link.h"
#include "work_task.h"
#include "attribute.h"
#include "server.h"
#include "credential.h"
#include "batch_request.h"
#include "pbs_job.h"
#include "pbs_error.h"
#include "log.h"
#include "../lib/Liblog/pbs_log.h"
#include "../lib/Liblog/log_event.h"
#include "acct.h"
#include "svrfunc.h"
#include "ji_mutex.h"
#include "mutex_mgr.hpp"
#include "svr_func.h" /* get_svr_attr_* */
#include "job_func.h" /* get_svr_attr_* */


/* Private Function local to this file */

/* Global Data Items: */

extern int   LOGLEVEL;
extern char *msg_manager;
extern char *msg_jobrerun;

extern void rel_resc(job *);

extern job  *chk_job_request(char *, struct batch_request *);
extern int issue_signal(job **, const char *, void(*)(batch_request *), void *, char *);

int finalize_rerunjob(struct batch_request *preq,job *pjob,int rc);

void delay_and_send_sig_kill(batch_request *preq_sig);

void send_sig_kill(struct work_task *pwt);

void post_rerun(batch_request *preq);


/*
 * delay_and_send_sig_kill
 *
 * The SIGTERM signal has been sent. Set a delay and prepare to send the SIGKILL.
 *
 */

void delay_and_send_sig_kill(
    
  batch_request *preq_sig)

  {
  int                   delay = 0;
  job                  *pjob;

  pbs_queue            *pque;

  batch_request        *preq_clt = NULL;  /* original client request */
  int                   rc;
  time_t                time_now = time(NULL);
  char    log_buf[LOCAL_LOG_BUF_SIZE];

  if (preq_sig == NULL)
    return;

  rc = preq_sig->rq_reply.brp_code;

  if (preq_sig->rq_extend != NULL)
    {
    preq_clt = get_remove_batch_request(preq_sig->rq_extend);
    }
  free_br(preq_sig);

  /* the client request has been handled another way, nothing left to do */
  if (preq_clt == NULL)
    return;

  if ((pjob = chk_job_request(preq_clt->rq_ind.rq_rerun, preq_clt)) == NULL)
    {
    /* job has gone away, chk_job_request() calls req_reject() on failure */
    return;
    }

  mutex_mgr pjob_mutex = mutex_mgr(pjob->ji_mutex, true);

  if (rc)
    {
    /* mom rejected request */

    if (rc == PBSE_UNKJOBID)
      {
      /* MOM claims no knowledge, so just purge it */
      log_event(
        PBSEVENT_JOB,
        PBS_EVENTCLASS_JOB,
        pjob->ji_qs.ji_jobid,
        "MOM rejected signal during rerun");

      /* removed the resources assigned to job */

      free_nodes(pjob);

      set_resc_assigned(pjob, DECR);

      unlock_ji_mutex(pjob, __func__, "3", LOGLEVEL);

      svr_job_purge(pjob);

      reply_ack(preq_clt);
      }
    else
      {
      pjob_mutex.unlock();
      req_reject(rc, 0, preq_clt, NULL, NULL);
      }

    return;
    }

  if ((pque = get_jobs_queue(&pjob)) != NULL)
    {
    mutex_mgr pque_mutex = mutex_mgr(pque->qu_mutex, true);
    mutex_mgr server_mutex = mutex_mgr(server.sv_attr_mutex, false);

    delay = attr_ifelse_long(&pque->qu_attr[QE_ATR_KillDelay],
                           &server.sv_attr[SRV_ATR_KillDelay],
                           DEFAULT_KILL_DELAY);
    }
  else
    {
    /* why is the pque null. Something went wrong */
    snprintf(log_buf, LOCAL_LOG_BUF_SIZE, "jobid %s returned a null queue", pjob->ji_qs.ji_jobid);
    req_reject(PBSE_UNKQUE, 0, preq_clt, NULL, log_buf);
    return;
    }

  pjob_mutex.unlock();
  reply_ack(preq_clt);
  set_task(WORK_Timed, delay + time_now, send_sig_kill, strdup(pjob->ji_qs.ji_jobid), FALSE);
  }

/*
 * send_sig_kill
 *
 * The SIGTERM has been sent and we've waited for the kill_delay so now send the SIGKILL.
 * @pre-cond: pwt must point to a valid task
 * @pre-cond: pwt->wt_parm1 must point to a valid character string
 *
 */
void send_sig_kill(
    
  struct work_task *pwt)

  {
  job                  *pjob;
  char                 *job_id = (char *)pwt->wt_parm1;
  static const char    *rerun = "rerun";

  free(pwt->wt_mutex);
  free(pwt);

  if (job_id == NULL)
    return;

  if ((pjob = svr_find_job(job_id, FALSE)) == NULL)
    {
    free(job_id);
    return;
    }
  
  char *extra = strdup(rerun);

  free(job_id);

  if (issue_signal(&pjob, "SIGKILL", post_rerun, extra,NULL) == 0)
    {
    pjob->ji_qs.ji_substate = JOB_SUBSTATE_RERUN;
    pjob->ji_qs.ji_svrflags = (pjob->ji_qs.ji_svrflags &
        ~(JOB_SVFLG_CHECKPOINT_FILE |JOB_SVFLG_CHECKPOINT_MIGRATEABLE |
          JOB_SVFLG_CHECKPOINT_COPIED)) | JOB_SVFLG_HASRUN;
    }

  unlock_ji_mutex(pjob, __func__, "6", LOGLEVEL);
  } /* END send_sig_kill() */


/*
 * post_rerun - handler for reply from mom on signal_job sent in req_rerunjob
 * If mom acknowledged the signal, then all is ok.
 * If mom rejected the signal for unknown jobid, then force local requeue.
 */

void post_rerun(

  batch_request *preq)

  {
  int   newstate;
  int   newsub;
  job  *pjob;

  char  log_buf[LOCAL_LOG_BUF_SIZE];

  if (preq == NULL)
    return;

  if (preq->rq_reply.brp_code != 0)
    {
    sprintf(log_buf, "rerun signal reject by mom: %s - %d", preq->rq_ind.rq_signal.rq_jid, preq->rq_reply.brp_code);
    log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,__func__,log_buf);

    if ((pjob = svr_find_job(preq->rq_ind.rq_signal.rq_jid, FALSE)))
      {
      mutex_mgr job_mutex(pjob->ji_mutex, true);
      
      svr_evaljobstate(*pjob, newstate, newsub, 1);
      svr_setjobstate(pjob, newstate, newsub, FALSE);
      }
    }

  free_br(preq);

  return;
  }  /* END post_rerun() */



/*
 * requeue_job_without_contacting_mom()
 *
 */
void requeue_job_without_contacting_mom(

  job &pjob)

  {
  if (pjob.ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    rel_resc(&pjob);
    svr_setjobstate(&pjob, JOB_STATE_QUEUED, JOB_SUBSTATE_QUEUED, FALSE);
    pjob.ji_wattr[JOB_ATR_exec_host].at_flags &= ~ATR_VFLAG_SET;

    if (pjob.ji_wattr[JOB_ATR_exec_host].at_val.at_str != NULL)
      {
      free(pjob.ji_wattr[JOB_ATR_exec_host].at_val.at_str);
      pjob.ji_wattr[JOB_ATR_exec_host].at_val.at_str = NULL;
      }
    }
  } /* END requeue_job_without_contacting_mom() */



/*
 * handle_requeue_all
 *
 * requeues all jobs without contacting the moms that the jobs should be using
 */

int handle_requeue_all(

  batch_request *preq)

  {
  int                rc;
  job               *pjob;
  all_jobs_iterator *iter;

  if ((preq->rq_perm & (ATR_DFLAG_MGWR)) == 0)
    {
    rc = PBSE_PERM;
    req_reject(rc, 0, preq, NULL, "You must be a manager to requeue all jobs");
    return(rc);
    }

  alljobs.lock();
  iter = alljobs.get_iterator();
  alljobs.unlock();

  while ((pjob = next_job(&alljobs, iter)) != NULL)
    {
    mutex_mgr job_mutex(pjob->ji_mutex, true);
    requeue_job_without_contacting_mom(*pjob);
    }

  reply_ack(preq);

  return(PBSE_NONE);
  } /* END handle_requeue_all() */



/**
 * req_rerunjob - service the Rerun Job Request
 *
 * This request Reruns a job by:
 * sending to MOM a signal job request with SIGKILL
 * marking the job as being rerun by setting the substate.
 *
 * NOTE:  can be used to requeue active jobs or completed jobs.
 */

int req_rerunjob(
   
  batch_request *preq)

  {
  int     rc = PBSE_NONE;
  job    *pjob;

  int     MgrRequired = TRUE;
  char    log_buf[LOCAL_LOG_BUF_SIZE];

  /* check if requestor is admin, job owner, etc */
  if (!strcasecmp(preq->rq_ind.rq_rerun, "all"))
    {
    return(handle_requeue_all(preq));
    }
  
  if ((pjob = chk_job_request(preq->rq_ind.rq_rerun, preq)) == 0)
    {
    /* FAILURE */

    /* chk_job_request calls req_reject() */

    rc = PBSE_SYSTEM;
    return rc; /* This needs to fixed to return an accurate error */
    }

  mutex_mgr pjob_mutex = mutex_mgr(pjob->ji_mutex, true);

  /* the job must be running or completed */

  if (pjob->ji_qs.ji_state >= JOB_STATE_EXITING)
    {
    if (pjob->ji_wattr[JOB_ATR_checkpoint_name].at_flags & ATR_VFLAG_SET)
      {
      /* allow end-users to rerun checkpointed jobs */

      MgrRequired = FALSE;
      }
    }
  else if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /* job is running */

    /* NO-OP */
    }
  else
    {
    /* FAILURE - job is in bad state */
    rc = PBSE_BADSTATE;
    snprintf(log_buf, LOCAL_LOG_BUF_SIZE, "job %s is in a bad state",
        preq->rq_ind.rq_rerun);
    req_reject(rc, 0, preq, NULL, log_buf);
    return rc;
    }

  if ((MgrRequired == TRUE) &&
      ((preq->rq_perm & (ATR_DFLAG_MGWR | ATR_DFLAG_OPWR)) == 0))
    {
    /* FAILURE */

    rc = PBSE_PERM;
    snprintf(log_buf, LOCAL_LOG_BUF_SIZE,
        "additional permissions required (ATR_DFLAG_MGWR | ATR_DFLAG_OPWR)");
    req_reject(rc, 0, preq, NULL, log_buf);
    return rc;
    }

  /* the job must be rerunnable */

  if (pjob->ji_wattr[JOB_ATR_rerunable].at_val.at_long == 0)
    {
    /* NOTE:  should force override this constraint? maybe (???) */
    /*          no, the user is saying that the job will break, and
                IEEE Std 1003.1 specifically says rerun is to be rejected
                if rerunable==FALSE -garrick */

    rc = PBSE_NORERUN;
    snprintf(log_buf, LOCAL_LOG_BUF_SIZE, "job %s not rerunnable",
        preq->rq_ind.rq_rerun);
    req_reject(rc, 0, preq, NULL, log_buf);
    return rc;
    }

  if (pjob->ji_qs.ji_state == JOB_STATE_RUNNING)
    {
    /* ask MOM to kill off the job if it is running */
    int                 delay = 0;
    pbs_queue          *pque;

    if ((pque = get_jobs_queue(&pjob)) != NULL)
      {
      mutex_mgr pque_mutex = mutex_mgr(pque->qu_mutex, true);
      mutex_mgr server_mutex = mutex_mgr(server.sv_attr_mutex, false);

      delay = attr_ifelse_long(&pque->qu_attr[QE_ATR_KillDelay],
                             &server.sv_attr[SRV_ATR_KillDelay],
                             0);
      }
    else
      {
      /* why is the pque null. Something went wrong */
      snprintf(log_buf, LOCAL_LOG_BUF_SIZE, "jobid %s returned a null queue", pjob->ji_qs.ji_jobid);
      req_reject(PBSE_UNKQUE, 0, preq, NULL, log_buf);
      return(PBSE_UNKQUE);
      }
    
    pjob->ji_qs.ji_substate = JOB_SUBSTATE_RERUN;

    if(delay != 0)
      {
      static const char *rerun = "rerun";
      char               *extra = strdup(rerun);

      get_batch_request_id(preq);
      if ((rc = issue_signal(&pjob, "SIGTERM", delay_and_send_sig_kill, extra, strdup(preq->rq_id))))
        {
        /* cant send to MOM */
        req_reject(rc, 0, preq, NULL, NULL);
        }
      return rc;
      }
    else
      {
      static const char *rerun = "rerun";
      char               *extra = strdup(rerun);

      rc = issue_signal(&pjob, "SIGKILL", post_rerun, extra, NULL);
      }
    }
  else
    { 
    if (pjob->ji_wattr[JOB_ATR_hold].at_val.at_long == HOLD_n)
      {
      svr_setjobstate(pjob, JOB_STATE_QUEUED, JOB_SUBSTATE_QUEUED, FALSE);
      }
    else
      {
      svr_setjobstate(pjob, JOB_STATE_HELD, JOB_SUBSTATE_HELD, FALSE);
      }

    /* reset some job attributes */
    
    pjob->ji_wattr[JOB_ATR_comp_time].at_flags &= ~ATR_VFLAG_SET;
    pjob->ji_wattr[JOB_ATR_reported].at_flags &= ~ATR_VFLAG_SET;

    set_statechar(pjob);

    rc = -1;
    }

  /* finalize_rerunjob will return with pjob->ji_mutex unlocked */
  pjob_mutex.set_unlock_on_exit(false);
  return finalize_rerunjob(preq,pjob,rc);
  }

/*
 * finalize_rerunjob
 *
 * The SIGKILL or SIGTERM - delay - SIGKILL has been sent to the job
 * now mark its substate as JOB_SUBSTATE_RERUN
 */


int finalize_rerunjob(
    
  batch_request *preq,
  job           *pjob,
  int            rc)

  {
  int       Force;
  char      log_buf[LOCAL_LOG_BUF_SIZE];

  if (pjob == NULL)
    return(PBSE_BAD_PARAMETER);

  mutex_mgr pjob_mutex = mutex_mgr(pjob->ji_mutex, true);

  if (preq->rq_extend && !strncasecmp(preq->rq_extend, RERUNFORCE, strlen(RERUNFORCE)))
    Force = 1;
  else
    Force = 0;

  switch (rc)
    {

    case -1:

      /* completed job was requeued */

      /* clear out job completion time if there is one */
      break;

    case 0:

      /* requeue request successful */

      pjob->ji_qs.ji_substate = JOB_SUBSTATE_RERUN;

      break;

    case PBSE_SYSTEM: /* This may not be accurate...*/
      rc = PBSE_MEM_MALLOC;
      snprintf(log_buf, LOCAL_LOG_BUF_SIZE, "Can not allocate memory");
      req_reject(rc, 0, preq, NULL, log_buf);
      return rc;
      break;

    default:

      if (Force == 0)
        {
        rc = PBSE_MOMREJECT;
        snprintf(log_buf, LOCAL_LOG_BUF_SIZE, "Rejected by mom");
        req_reject(rc, 0, preq, NULL, log_buf);
        return rc;
        }
      else
        {
        int           newstate;
        int           newsubst;
        unsigned int  dummy;
        char         *tmp;
        long          cray_enabled = FALSE;
       
        get_svr_attr_l(SRV_ATR_CrayEnabled, &cray_enabled);

        if ((cray_enabled == TRUE) &&
            (pjob->ji_wattr[JOB_ATR_login_node_id].at_val.at_str != NULL))
          tmp = parse_servername(pjob->ji_wattr[JOB_ATR_login_node_id].at_val.at_str, &dummy);
        else
          tmp = parse_servername(pjob->ji_wattr[JOB_ATR_exec_host].at_val.at_str, &dummy);

        /* Cannot communicate with MOM, forcibly requeue job.
           This is a relatively disgusting thing to do */

        sprintf(log_buf, "rerun req to %s failed (rc=%d), forcibly requeueing job",
          tmp, rc);

        free(tmp);

        log_event(
          PBSEVENT_ERROR | PBSEVENT_ADMIN | PBSEVENT_JOB,
          PBS_EVENTCLASS_JOB,
          pjob->ji_qs.ji_jobid,
          log_buf);

        log_err(-1, __func__, log_buf);

        strcat(log_buf, ", previous output files may be lost");

        svr_mailowner(pjob, MAIL_OTHER, MAIL_FORCE, log_buf);

        svr_setjobstate(pjob, JOB_STATE_EXITING, JOB_SUBSTATE_RERUN3, FALSE);

        rel_resc(pjob); /* free resc assigned to job */

        if ((pjob->ji_qs.ji_svrflags & JOB_SVFLG_HOTSTART) == 0)
          {
          /* in case of server shutdown, don't clear exec_host */
          /* will use it on hotstart when next comes up        */
          
          job_attr_def[JOB_ATR_exec_host].at_free(&pjob->ji_wattr[JOB_ATR_exec_host]);

          job_attr_def[JOB_ATR_session_id].at_free(&pjob->ji_wattr[JOB_ATR_session_id]);
          
          job_attr_def[JOB_ATR_exec_gpus].at_free(&pjob->ji_wattr[JOB_ATR_exec_gpus]);
          }

        pjob->ji_modified = 1;    /* force full job save */

        pjob->ji_momhandle = -1;
        pjob->ji_qs.ji_svrflags &= ~JOB_SVFLG_StagedIn;

        svr_evaljobstate(*pjob, newstate, newsubst, 0);
        svr_setjobstate(pjob, newstate, newsubst, FALSE);
        }

      break;
    }  /* END switch (rc) */

  pjob->ji_qs.ji_svrflags = (pjob->ji_qs.ji_svrflags &
      ~(JOB_SVFLG_CHECKPOINT_FILE |JOB_SVFLG_CHECKPOINT_MIGRATEABLE |
        JOB_SVFLG_CHECKPOINT_COPIED)) | JOB_SVFLG_HASRUN;

  sprintf(log_buf, msg_manager, msg_jobrerun, preq->rq_user, preq->rq_host);
  log_event(PBSEVENT_JOB,PBS_EVENTCLASS_JOB,pjob->ji_qs.ji_jobid,log_buf);

  reply_ack(preq);

  /* note in accounting file */
  account_record(PBS_ACCT_RERUN, pjob, NULL);

  return rc;
  }  /* END req_rerunjob() */


/* END req_rerun.c */

