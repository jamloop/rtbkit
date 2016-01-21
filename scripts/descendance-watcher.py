#!/usr/bin/env python

# make sure to pip install psutil before running this

import logging
import os
import psutil
import subprocess
import sys
import time

FORMAT='%(asctime)s %(name)s:%(levelname)s: %(message)s'
logging.basicConfig(level=logging.INFO, format=FORMAT)

def restart_services():
    logging.info('Restarting services...')

    # We need to fork off a daemon and let it restart the stack from 'outside'
    try: 
        pid = os.fork() 
        if pid > 0:
            # In parent - wait a bit and die
            # usually not reached, the child will restart us before
            time.sleep(60)
            sys.exit(0) 
    except OSError, e: 
        print >>sys.stderr, "fork #1 failed: %d (%s)" % (e.errno, e.strerror) 
        sys.exit(1)

    # in child
    # decouple from parent environment
    os.chdir("/") 
    os.setsid() 
    os.umask(0) 

    # do second fork
    try: 
        pid = os.fork() 
        if pid > 0:
            # exit from second parent
            sys.exit(0) 
    except OSError, e: 
        print >>sys.stderr, "fork #2 failed: %d (%s)" % (e.errno, e.strerror) 
        sys.exit(1) 

    # Reset tmux env to avoid the 'nesting with care' error
    try:
        del(os.environ['TMUX'])
    except KeyError:
        pass

    # We're outside. restart the stack
    subprocess.check_call('. /home/rtbkit/.profile && launch-rtb', shell=True)


def main():
    start_delay = 75  # Wait this many seconds before grabbing initial process list

    action = restart_services
    root_process = psutil.Process(os.getppid())
    if root_process.pid == 1:
        logging.error('Parent has PID 1. that will not work...')
        exit(1)

    logging.info('Starting up. Waiting {start_delay}s before getting process list'.format(start_delay=start_delay))
    time.sleep(start_delay)

    initial_proc_set = set(root_process.children(recursive=False))
    logging.info('Running.  Initial process set: {initial_proc_set}'.format(initial_proc_set=initial_proc_set))
    need_action = False

    while not need_action:
        current_proc_set = set(root_process.children(recursive=False))
        if current_proc_set != initial_proc_set:
            need_action = True
        time.sleep(30)

    if need_action:
        logging.error('Process sets differ. Engage evasive maneuver: {diff}'.format(diff=initial_proc_set - current_proc_set))
        action()



if __name__ == '__main__':
    main()
