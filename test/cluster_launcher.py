import os
import sys
import time
import tempfile
import subprocess
from cStringIO import StringIO
from redistrib import command


def launch():
    template = '''
    daemonize yes
    port {port}
    cluster-node-timeout 5000
    pidfile {tmpdir}/redis_cluster_node-{port}.pid
    logfile {tmpdir}/redis_cluster_node-{port}.log
    save ""
    appendonly no
    cluster-enabled yes
    cluster-config-file {tmpdir}/redis_cluster_node-{port}.conf
    '''

    for i in xrange(4):
        p = subprocess.Popen(['redis-server', '-'], stdin=subprocess.PIPE)
        p.communicate(input=template.format(
            tmpdir=tempfile.gettempdir(), port=8800 + i))
    time.sleep(1)
    command.start_cluster_on_multi(
        [('127.0.0.1', 8800 + i) for i in xrange(4)])


def kill():
    for i in xrange(4):
        try:
            with open('{tmpdir}/redis_cluster_node-{port}.pid'.format(
                    tmpdir=tempfile.gettempdir(), port=8800 + i), 'r') as f:
                pid = int(f.read())
                subprocess.call(['kill', str(pid)])
            os.remove('{tmpdir}/redis_cluster_node-{port}.conf'.format(
                tmpdir=tempfile.gettempdir(), port=8800 + i))
        except (IOError, OSError):
            pass

if __name__ == '__main__':
    launch() if sys.argv[0] == 'launch' else kill()
