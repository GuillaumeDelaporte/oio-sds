# -*- coding: utf-8 -*-
import os
import mock

from oio.common.utils import xattr
from oio.blob.indexer import BlobIndexer
from oio.blob.utils import chunk_xattr_keys
from oio.common.exceptions import FaultyChunk
from oio.rdir.client import RdirClient
from tests.utils import BaseTestCase, random_id


class TestIndexerCrawler(BaseTestCase):
    def setUp(self):
        super(TestIndexerCrawler, self).setUp()

        self.namespace = self.conf['namespace']

        self.rawx_conf = self.conf['services']['rawx'][0]
        self.conf = {"namespace": self.namespace,
                     "volume": self.rawx_conf['path']}
        self.rdir_client = RdirClient(self.conf)

    def tearDown(self):
        super(TestIndexerCrawler, self).tearDown()

    def _create_chunk(self, rawx_path, alias="toto"):
        container_id = random_id(64)
        content_id = random_id(32)
        chunk_id = random_id(64)

        chunk_dir = "%s/%s" % (rawx_path, chunk_id[0:3])
        if not os.path.isdir(chunk_dir):
            os.makedirs(chunk_dir)

        chunk_path = "%s/%s" % (chunk_dir, chunk_id)
        with open(chunk_path, "w") as f:
            f.write("toto")

        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['chunk_hash'], 32 * '0')
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['chunk_id'], chunk_id)
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['chunk_pos'], '0')
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['chunk_size'], '4')
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['container_id'],
            container_id)
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['content_id'], content_id)
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['content_path'], alias)
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['content_policy'],
            'TESTPOLICY')
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['content_chunkmethod'],
            'plain/nb_copy=3')
        xattr.setxattr(
            chunk_path, 'user.' + chunk_xattr_keys['content_version'], '0')

        return chunk_path, container_id, content_id, chunk_id

    def _rdir_get(self, rawx_addr, container_id, content_id, chunk_id):
        data = self.rdir_client.chunk_fetch(rawx_addr)
        key = (container_id, content_id, chunk_id)
        for i_container, i_content, i_chunk, i_value in data:
            if (i_container, i_content, i_chunk) == key:
                return i_value
        return None

    def _test_index_chunk(self, alias="toto"):

        # create a fake chunk
        chunk_path, container_id, content_id, chunk_id = self._create_chunk(
            self.rawx_conf['path'], alias)

        # index the chunk
        indexer = BlobIndexer(self.conf)

        with mock.patch('oio.blob.indexer.time.time',
                        mock.MagicMock(return_value=1234)):
            indexer.update_index(chunk_path)

        # check rdir
        check_value = self._rdir_get(self.rawx_conf['addr'], container_id,
                                     content_id, chunk_id)

        self.assertIsNotNone(check_value)

        self.assertEqual(check_value['mtime'], 1234)

        # index a chunk already indexed
        with mock.patch('oio.blob.indexer.time.time',
                        mock.MagicMock(return_value=4567)):
            indexer.update_index(chunk_path)

        # check rdir
        check_value = self._rdir_get(self.rawx_conf['addr'], container_id,
                                     content_id, chunk_id)

        self.assertIsNotNone(check_value)

        self.assertEqual(check_value['mtime'], 4567)

    def test_index_chunk(self):
        return self._test_index_chunk()

    def test_index_unicode_chunk(self):
        return self._test_index_chunk('a%%%s%d%xàç"\r\n{0}€ 1+1=2/\\$\t_')

    def test_index_chunk_missing_xattr(self):
        # create a fake chunk
        chunk_path, container_id, content_id, chunk_id = self._create_chunk(
            self.rawx_conf['path'])

        # remove mandatory xattr
        xattr.removexattr(
            chunk_path, 'user.' + chunk_xattr_keys['container_id'])

        # try to index the chunk
        indexer = BlobIndexer(self.conf)

        self.assertRaises(FaultyChunk, indexer.update_index, chunk_path)
