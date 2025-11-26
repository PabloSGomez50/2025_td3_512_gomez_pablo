from pytz import timezone
from apscheduler.schedulers.asyncio import AsyncIOScheduler


argentina_tz = timezone("America/Argentina/Buenos_Aires")
arg_scheduler = AsyncIOScheduler(timezone=argentina_tz)