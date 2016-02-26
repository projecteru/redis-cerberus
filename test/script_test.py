import time
import string
import subprocess
import unittest
import redis

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
        self.t = redis.Redis(host='127.0.0.1', port=27182)

    def test_key_commands(self):
        self.assertTrue(self.t.set('quick', 'brown fox'))
        self.assertEqual('brown fox', self.t.get('quick'))
        self.assertTrue(self.t.set('year', '2010'))
        self.assertEqual(2011, self.t.incr('year'))
        self.assertEqual('2011', self.t.get('year'))

    def test_list_commands(self):
        self.assertEqual(1, self.t.lpush('list0', 'xxx'))
        self.assertEqual(['xxx'], self.t.lrange('list0', '0', '-1'))
        self.assertEqual(3, self.t.lpush('list0', 'yyy', 'zzz'))
        self.assertEqual(['zzz', 'yyy', 'xxx'],
                         self.t.lrange('list0', '0', '-1'))
        self.assertEqual(2, self.t.lpush('list1', 'aaa', 'ddd'))
        self.assertEqual(4, self.t.lpush('list1', 'bbb', 'ccc'))
        self.assertEqual(['bbb', 'ddd'], self.t.lrange('list1', '1', '2'))
        self.assertEqual(2, self.t.delete('list0', 'list1'))

    def test_set_commands(self):
        self.assertEqual(1, self.t.sadd('sssset', 'xxx'))
        self.assertEqual('xxx', self.t.srandmember('sssset'))
        self.assertEqual(2, self.t.sadd('sssset', 'yyy', 'zzz'))
        self.assertEqual({'xxx', 'yyy', 'zzz'}, self.t.smembers('sssset'))
        self.assertEqual(1, self.t.delete('sssset'))

    def test_multiple_keys_commands(self):
        p = string.digits + string.ascii_lowercase + string.ascii_uppercase
        for c in p:
            self.assertTrue(self.t.mset({c + d: d + c for d in p}))
        for c in p:
            self.assertEqual([c + b for b in p],
                             self.t.mget(*[b + c for b in p]))
        for c in p:
            self.assertEqual(len(p), self.t.delete(*[b + c for b in p]))

    def test_multiple_keys_commands_for_single_key(self):
        self.assertTrue(self.t.mset({'k078': 'v078'}))
        self.assertEqual(['v078'], self.t.mget('k078'))
        self.assertEqual(1, self.t.delete('k078'))
        self.assertEqual(0, self.t.delete('k078'))

    def test_multiple_del(self):
        self.assertTrue(self.t.mset({
            'k078': 'v078',
            'kk078': 'vv078',
            'k20160229': 'v20160229',
        }))
        self.assertEqual(2, self.t.delete('k078', 'kk078', 'kkk078', '-'))
        self.assertEqual(1, self.t.delete('k078', 'kk078', 'k20160229'))
        self.assertEqual(0, self.t.delete('k078', 'kk078', 'k20160229'))

    def test_bulk_commands(self):
        p = string.digits + string.ascii_lowercase + string.ascii_uppercase
        pipe = self.t.pipeline(transaction=False)
        for c in p:
            for d in p:
                pipe.set('.' + c + d, d + c)
            self.assertEqual([True] * len(p), pipe.execute())
        pipe = self.t.pipeline(transaction=False)
        for c in p:
            for b in p:
                pipe.get('.' + b + c)
            self.assertEqual([c + b for b in p], pipe.execute())

    def test_large_pipes(self):
        pipe = self.t.pipeline(transaction=False)
        for i in xrange(1000):
            pipe.set('Key:%016d' % i, 'Value:%026d' % i)
        self.assertEqual([True] * 1000, pipe.execute())

        pipe = self.t.pipeline(transaction=False)
        for i in xrange(2000):
            pipe.get('Key:%016d' % i)
        r = pipe.execute()
        self.assertEqual(2000, len(r))
        self.assertEqual(['Value:%026d' % i for i in xrange(1000)], r[:1000])
        self.assertEqual([None] * 1000, r[1000:])

    def test_list_pipes(self):
        pipe = self.t.pipeline(transaction=False)
        for i in xrange(1000):
            pipe.lpush('list_bulk', 'Value:%026d' % i)
        self.assertEqual(range(1, 1001), pipe.execute())

        pipe = self.t.pipeline(transaction=False)
        for i in xrange(500):
            pipe.lrange('list_bulk', '%d' % i, '%d' % (i + 5))
        r = pipe.execute()
        for i in xrange(500):
            expected = ['Value:%026d' % (999 - j - i) for j in xrange(6)]
            self.assertEqual(expected, r[i],
                             msg='at %d: %s <> %s' % (i, expected, r[i]))

    def test_eval(self):
        r = self.t.eval('return KEYS[1]', '1', 'a')
        self.assertEqual('a', r)

if __name__ == '__main__':
    main()
