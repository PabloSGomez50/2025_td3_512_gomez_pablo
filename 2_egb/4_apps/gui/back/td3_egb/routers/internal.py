from typing import Annotated
from fastapi import APIRouter, HTTPException, Depends, Body
from starlette import status

import logging

from td3_egb.settings import settings
from td3_egb.utils import enviar_uart, read_uart
from td3_egb.dependencies import DevReaderInst

log = logging.getLogger("api")

router = APIRouter(
    prefix="/internal",
    tags=["Uso interno"]
)


@router.get(
    "/healthcheck/",
    status_code=status.HTTP_200_OK,
    name="healthcheck"
)
async def healthcheck():
    """Healthcheck endpoint."""
    return {"status": "ok", "message": "API is healthy"}

@router.get(
    "/reader/queues",
    status_code=status.HTTP_200_OK,
    name="Get Reader Queues"
)
async def get_reader_queues(reader: DevReaderInst):
    """Get current queues registered in DeviceReader."""
    try:
        queues_info = {}
        for key, qset in reader.queues.items():
            queues_info[key] = [id(q) for q in qset]
        return {"status": "ok", "queues": queues_info}
    except Exception as e:
        log.error(f"Error getting reader queues: {e}")
        raise HTTPException(status_code=500, detail=f"Error getting reader queues: {e}")

@router.post(
    "/echo",
    status_code=status.HTTP_200_OK,
    name="Echo Endpoint"
)
async def echo_endpoint(msg: Annotated[str, Body(embed=True)]):
    """Echo the received data."""
    
    try:
        response = enviar_uart("$echo " + msg)
    except Exception as e:
        log.error(f"Error in echo endpoint: {e}")
        raise HTTPException(status_code=500, detail=f"Error in echo endpoint: {e}")
    log.info(f"Echo response: {response}")
    return {"data": response}


@router.post(
    "/msg",
    status_code=status.HTTP_200_OK,
    name="Echo Endpoint"
)
async def msg_egb(msg: Annotated[str, Body(embed=True)]):
    """Echo the received data."""
    
    try:
        response = enviar_uart(msg)
    except Exception as e:
        log.error(f"Error in msg endpoint: {e}")
        raise HTTPException(status_code=500, detail=f"Error in msg endpoint: {e}")
    log.info(f"Msg response: {response}")
    return {"data": response}

@router.post(
    "/read_data",
    status_code=status.HTTP_200_OK,
    name="Read Data from EGB"
)
async def read_data_egb():
    """Read data from EGB data channel."""
    
    try:
        return {
            "data": read_uart(timeout=0.5, path_dev=settings.DEV_DATA_PATH)
        }
    except Exception as e:
        log.error(f"Error in read_data endpoint: {e}")
        raise HTTPException(status_code=500, detail=f"Error in read_data endpoint: {e}")
    


if settings.SCHEDULER_ENABLED:
    @router.get(
        "/scheduler-health",
        status_code=status.HTTP_200_OK,
        name="scheduler-health"
    )
    async def scheduler_health():
        """Healthcheck for the scheduler."""
        try:
            running = getattr(arg_scheduler, "running", False)
            jobs = arg_scheduler.get_jobs()
            return {"status": "ok", "running": running, "jobs_count": len(jobs)}
        except Exception as e:
            log.error(f"Error in scheduler healthcheck: {e}")
            raise HTTPException(status_code=500, detail=f"Error in scheduler healthcheck: {e}")


    @router.get(
        "/scheduler",
        status_code=status.HTTP_200_OK,
        name="scheduler-jobs"
    )
    async def scheduler_jobs():
        """List jobs with basic metadata."""
        try:
            jobs_info = []
            for job in arg_scheduler.get_jobs():
                jobs_info.append({
                    "id": getattr(job, "id", None),
                    "name": getattr(job, "name", None),
                    "next_run_time": job.next_run_time.isoformat() if job.next_run_time else None,
                    "trigger": type(job.trigger).__name__,
                    "paused": job.next_run_time is None,
                })
            return {"status": "ok", "jobs": jobs_info}
        except Exception as e:
            log.error(f"Error listing scheduler jobs: {e}")
            raise HTTPException(status_code=500, detail=f"Error listing scheduler jobs: {e}")


    @router.post(
        "/scheduler/{job_id}/pause",
        status_code=status.HTTP_200_OK,
        name="scheduler-job-pause"
    )
    async def scheduler_job_pause(job_id: str):
        """Pause a scheduled job by id."""
        try:
            arg_scheduler.pause_job(job_id)
            return {"status": "ok", "message": f"Job {job_id} paused"}
        except Exception as e:
            log.error(f"Error pausing job {job_id}: {e}")
            raise HTTPException(status_code=500, detail=f"Error pausing job {job_id}: {e}")


    @router.post(
        "/scheduler/{job_id}/resume",
        status_code=status.HTTP_200_OK,
        name="scheduler-job-resume"
    )
    async def scheduler_job_resume(job_id: str):
        """Resume a paused job by id."""
        try:
            arg_scheduler.resume_job(job_id)
            return {"status": "ok", "message": f"Job {job_id} resumed"}
        except Exception as e:
            log.error(f"Error resuming job {job_id}: {e}")
            raise HTTPException(status_code=500, detail=f"Error resuming job {job_id}: {e}")


    @router.post(
        "/scheduler/{job_id}/reschedule",
        status_code=status.HTTP_200_OK,
        name="scheduler-job-reschedule"
    )
    async def scheduler_job_reschedule(
        job_id: str,
        seconds: int = 0,
        minutes: int = 0,
        hours: int = 0,
        days: int = 0,
    ):
        """
        Reschedule a job's interval.
        Provide at least one of seconds/minutes/hours/days (> 0).
        This uses APScheduler.reschedule_job with trigger='interval'.
        """
        try:
            # Validate input
            if not any([seconds, minutes, hours, days]):
                raise HTTPException(status_code=400, detail="Provide at least one interval parameter (> 0)")

            trigger_args = {}
            if seconds:
                trigger_args["seconds"] = seconds
            if minutes:
                trigger_args["minutes"] = minutes
            if hours:
                trigger_args["hours"] = hours
            if days:
                trigger_args["days"] = days

            arg_scheduler.reschedule_job(job_id, trigger="interval", **trigger_args)
            return {"status": "ok", "message": f"Job {job_id} rescheduled", "interval": trigger_args}
        except HTTPException:
            raise
        except Exception as e:
            log.error(f"Error rescheduling job {job_id}: {e}")
            raise HTTPException(status_code=500, detail=f"Error rescheduling job {job_id}: {e}")
        