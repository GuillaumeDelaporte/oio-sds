import logging
import re
import socket
from urllib import quote

from eventlet import patcher
from eventlet.green.httplib import HTTPConnection, HTTPResponse, _UNKNOWN, \
        CONTINUE, HTTPMessage
from oio.common.utils import json

requests = patcher.import_patched('requests.__init__')


class CustomHTTPResponse(HTTPResponse):
    def __init__(self, sock, debuglevel=0, strict=0,
                 method=None):
        self.sock = sock
        self._actual_socket = sock.fd._sock
        self.fp = sock.makefile('rb')
        self.debuglevel = debuglevel
        self.strict = strict
        self._method = method

        self.msg = None

        self.version = _UNKNOWN
        self.status = _UNKNOWN
        self.reason = _UNKNOWN
        self.chunked = _UNKNOWN
        self.chunk_left = _UNKNOWN
        self.length = _UNKNOWN
        self.will_close = _UNKNOWN

    def expect_response(self):
        if self.fp:
            self.fp.close()
            self.fp = None
        self.fp = self.sock.makefile('rb', 0)
        version, status, reason = self._read_status()
        if status != CONTINUE:
            self._read_status = lambda: (version, status, reason)
            self.begin()
        else:
            self.status = status
            self.reason = reason
            self.version = 11
            self.msg = HTTPMessage(self.fp, 0)
            self.msg.fp = None

    def read(self, amount=None):
        return HTTPResponse.read(self, amount)

    def force_close(self):
        if self._actual_socket:
            self._actual_socket.close()
        self._actual_socket = None
        self.close()

    def close(self):
        HTTPResponse.close(self)
        self.sock = None
        self._actual_socket = None


class CustomHttpConnection(HTTPConnection):
    response_class = CustomHTTPResponse

    def connect(self):
        r = HTTPConnection.connect(self)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        return r

    def putrequest(self, method, url, skip_host=0, skip_accept_encoding=0):
        self._method = method
        self._path = url
        return HTTPConnection.putrequest(self, method, url, skip_host,
                                         skip_accept_encoding)

    def getresponse(self):
        response = HTTPConnection.getresponse(self)
        logging.debug('HTTP %s %s:%s %s',
                      self._method, self.host, self.port, self._path)
        return response


def http_request(host, method, path, headers=None, query_string=None,
                 body=None):
    headers = headers or {}

    if isinstance(body, dict):
        body = json.dumps(body)
        headers['Content-Type'] = 'application/json'
    headers['Content-Length'] = len(body)
    conn = http_connect(host, method, path, headers=headers,
                        query_string=query_string)
    conn.send(body)
    resp = conn.getresponse()
    body = resp.read()
    resp.close()
    conn.close()
    return resp, body


def http_connect(host, method, path, headers=None, query_string=None):
    if isinstance(path, unicode):
        try:
            path = path.encode('utf-8')
        except UnicodeError as e:
            logging.exception('ERROR encoding to UTF-8: %s', str(e))
    path = quote('/' + path)
    conn = CustomHttpConnection(host)
    if query_string:
        path += '?' + query_string
    conn.path = path
    conn.putrequest(method, path)
    if headers:
        for header, value in headers.items():
            if isinstance(value, list):
                for k in value:
                    conn.putheader(header, str(k))
            else:
                conn.putheader(header, str(value))
    conn.endheaders()
    return conn

_token = r'[^()<>@,;:\"/\[\]?={}\x00-\x20\x7f]+'
_ext_pattern = re.compile(
    r'(?:\s*;\s*(' + _token + r')\s*(?:=\s*(' + _token +
    r'|"(?:[^"\\]|\\.)*"))?)')


def parse_content_type(raw_content_type):
    param_list = []
    if raw_content_type:
        if ';' in raw_content_type:
            content_type, params = raw_content_type.split(';', 1)
            params = ';' + params
            for p in _ext_pattern.findall(params):
                k = p[0].strip()
                v = p[1].strip()
                param_list.append((k, v))
    return raw_content_type, param_list


_content_range_pattern = re.compile(r'^bytes (\d+)-(\d+)/(\d+)$')


def parse_content_range(raw_content_range):
    found = re.search(_content_range_pattern, raw_content_range)
    if not found:
        raise ValueError('invalid content-range %r' % (raw_content_range,))
    return tuple(int(x) for x in found.groups())


def http_header_from_ranges(ranges):
    s = 'bytes='
    for i, (start, end) in enumerate(ranges):
        if end:
            if end < 0:
                raise ValueError("Invalid range (%s, %s)" % (start, end))
            elif start is not None and end < start:
                raise ValueError("Invalid range (%s, %s)" % (start, end))
        else:
            if start is None:
                raise ValueError("Invalid range (%s, %s)" % (start, end))

        if start is not None:
            s += str(start)
        s += '-'

        if end is not None:
            s += str(end)
        if i < len(ranges) - 1:
            s += ','
    return s


def ranges_from_http_header(val):
    if not val.startswith('bytes='):
        raise ValueError('Invalid Range value: %s' % val)
    ranges = []
    for r in val[6:].split(','):
        start, end = r.split('-', 1)
        if start:
            start = int(start)
        else:
            start = None
        if end:
            end = int(end)
            if end < 0:
                raise ValueError('Invalid byterange value: %s' % val)
            elif start is not None and end < start:
                raise ValueError('Invalid byterange value: %s' % val)
        else:
            end = None
            if start is None:
                raise ValueError('Invalid byterange value: %s' % val)
        ranges.append((start, end))
    return ranges


class HeadersDict(dict):
    def __init__(self, headers, **kwargs):
        if headers:
            self.update(headers)
        self.update(kwargs)

    def update(self, data):
        if hasattr(data, 'keys'):
            for key in data.keys():
                self[key.title()] = data[key]
        else:
            for k, v in data:
                self[k.title()] = v

    def __setitem__(self, k, v):
        if v is None:
            self.pop(k.title(), None)
        return dict.__setitem__(self, k.title(), v)

    def get(self, k, default=None):
        return dict.get(self, k.title(), default)

    def pop(self, k, default=None):
        return dict.pop(self, k.title(), default)
