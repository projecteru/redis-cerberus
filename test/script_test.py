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

    def test_key_commands(self):
        self.assertEqual(
            'ok', self.t.talk('set', 'quick', 'brown fox').lower())
        self.assertEqual('brown fox', self.t.talk('get', 'quick'))
        self.assertEqual('ok', self.t.talk('set', 'year', '2010').lower())
        self.assertEqual(2011, self.t.talk('incr', 'year'))
        self.assertEqual('2011', self.t.talk('get', 'year'))

    def test_list_commands(self):
        self.assertEqual(1, self.t.talk('lPush', 'list0', 'xxx'))
        self.assertEqual(['xxx'], self.t.talk('lrange', 'list0', '0', '-1'))
        self.assertEqual(3, self.t.talk('lPush', 'list0', 'yyy', 'zzz'))
        self.assertEqual(['zzz', 'yyy', 'xxx'],
                         self.t.talk('lrange', 'list0', '0', '-1'))
        self.assertEqual(2, self.t.talk('lPush', 'list1', 'aaa', 'ddd'))
        self.assertEqual(4, self.t.talk('lPush', 'list1', 'bbb', 'ccc'))
        self.assertEqual(['bbb', 'ddd'],
                         self.t.talk('lrange', 'list1', '1', '2'))
        self.t.talk('del', 'list0', 'list1')

    def test_set_commands(self):
        self.assertEqual(1, self.t.talk('sadd', 'sssset', 'xxx'))
        self.assertEqual('xxx', self.t.talk('srandmember', 'sssset'))
        self.assertEqual(['xxx'], self.t.talk('smembers', 'sssset'))
        self.t.talk('del', 'sssset')

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

    def test_list_pipes(self):
        r = self.t.talk_bulk([['lpush', 'list_bulk', 'Value:%026d' % i]
                              for i in xrange(1000)])
        self.assertEqual(range(1, 1001), r)
        r = self.t.talk_bulk([['lrange', 'list_bulk', '%d' % i, '%d' % (i + 5)]
                              for i in xrange(500)])
        for i in xrange(500):
            expected = ['Value:%026d' % (999 - j - i) for j in xrange(6)]
            self.assertEqual(expected, r[i],
                             msg='at %d: %s <> %s' % (i, expected, r[i]))

    def test_eval(self):
        r = self.t.talk('eval', 'return KEYS[1]', '1', 'a')
        self.assertEqual('a', r)

if __name__ == '__main__':
    main()
