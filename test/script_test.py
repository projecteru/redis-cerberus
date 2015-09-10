import time
import string
import subprocess
import unittest
from redistrib.clusternode import Talker

import cluster_launcher


def main():
    cluster_launcher.kill()
    c = subprocess.Popen(['./cerberus', 'test/test-cerberus.conf'],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        cluster_launcher.launch()
        time.sleep(1)
        unittest.main()
    finally:
        c.terminate()
        cluster_launcher.kill()


class ScriptTest(unittest.TestCase):
    def setUp(self):
        self.t = Talker('127.0.0.1', 27182)

    def tearDown(self):
        self.t.close()

    def test_simple_commands(self):
        self.assertEqual(
            'ok', self.t.talk('set', 'quick', 'brown fox').lower())
        self.assertEqual('brown fox', self.t.talk('get', 'quick'))
        self.assertEqual('ok', self.t.talk('set', 'year', '2010').lower())
        self.assertEqual(2011, self.t.talk('incr', 'year'))
        self.assertEqual('2011', self.t.talk('get', 'year'))

    def test_multiple_keys_commands(self):
        p = string.digits + string.ascii_lowercase + string.ascii_uppercase
        for c in p:
            self.assertEqual('ok', self.t.talk(
                'mset', *sum([[c + d, d + c] for d in p], [])).lower())
        for c in p:
            self.assertEqual([c + b for b in p],
                             self.t.talk('mget', *[b + c for b in p]))

    def test_bulk_commands(self):
        p = string.digits + string.ascii_lowercase + string.ascii_uppercase
        for c in p:
            self.assertEqual(['OK'] * len(p), self.t.talk_bulk(
                [['set', '.' + c + d, d + c] for d in p]))
        for c in p:
            self.assertEqual([c + b for b in p], self.t.talk_bulk(
                [['get', '.' + b + c] for b in p]))

    def test_large_pipes(self):
        r = self.t.talk_bulk([['set', 'Key:%016d' % i, 'Value:%026d' % i]
                              for i in xrange(1000)])
        self.assertEqual(['OK'] * 1000, r)
        r = self.t.talk_bulk([['gEt', 'Key:%016d' % i] for i in xrange(2000)])
        self.assertEqual(2000, len(r))
        self.assertEqual(['Value:%026d' % i for i in xrange(1000)], r[:1000])
        self.assertEqual([None] * 1000, r[1000:])


if __name__ == '__main__':
    main()
