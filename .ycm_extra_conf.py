import logging
import logging.handlers 

from pathlib import Path


LOGGER = logging.getLogger('LOGGER')
LOGGER.setLevel(logging.DEBUG)
LOGGER.addHandler(logging.handlers.SysLogHandler(address='/dev/log'))

PROJ_ROOT = Path(__file__).parent

def Settings(**kwargs):
    
    settings =  {
        "flags" : [
            "-x", "c", 
            "-std=c99", "-Wall", 
            "-DDEBUG",
            "-DHOST_DEBUG",
            "-Ibsp",
            "-IToshiba-8c8w/",
            "-IToshiba-8c8w/nvme",
        ]
    }

    LOGGER.debug(f"YCM_EXTRA_CONF: kwargs={kwargs}")
    
    if kwargs["language"] == "cfamily":
        LOGGER.debug(f"YCM_EXTRA_CONF: use settings={settings}")
        return settings
