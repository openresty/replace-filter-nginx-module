Name
====

ngx_replace_filter - Streaming regular expression replacement in response bodies.

*This module is not distributed with the Nginx source.* See [the installation instructions](http://wiki.nginx.org/HttpLuaModule#Installation).

Status
======

This module is at the early phase of development and is considered highly experimental.

The user interface is still in flux and may be changed without notice.

Synopsis
========

    location /t {
        default_type text/html;
        echo abc;
        replace_filter 'ab|abc' X;
    }

    location / {
        # proxy_pass/fastcgi_pass/...

        # caseless global substitution:
        replace_filter '\d+' 'blah blah' 'ig';
        replace_filter_types text/plain text/css;
    }

Directives
==========

replace_filter
--------------
**syntax:** *replace_filter &lt;regex&gt; &lt;replace&gt;*

**syntax:** *replace_filter &lt;regex&gt; &lt;replace&gt; &lt;flags&gt;*

**default:** *no*

**context:** *http, server, location*

**phase:** *output body filter*

replace_filter_types
--------------------

**syntax:** *replace_filter_types &lt;mime-type&gt; ...*

**default:** *replace_filter_types text/html*

**context:** *http, server, location*

**phase:** *output body filter*

Installation
============

You need to install the sregex library first:

https://github.com/agentzh/sregex

And then rebuild your Nginx like this:

    ./configure --add-module=/path/to/replace-filter-nginx-module

If sregex is not installed to the default prefix (i.e., `/usr/local`), then
you should specify the locations of your sregex installation via
the `SREGEX_INC` and `SREGEX_LIB` environments before running the
`./configure` script, as in

    export SREGEX_INC=/opt/sregex/include
    export SREGEX_LIB=/opt/sregex/lib

assuming that your sregex is installed to the prefix `/opt/sregex`.

Copyright and License
=====================

This module is licensed under the BSD license.

Copyright (C) 2012, by Yichun "agentzh" Zhang (章亦春) <agentzh@gmail.com>.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

See Also
========

* agentzh's sregex library: https://github.com/agentzh/sregex
* the standard ngx_sub_filter module: http://nginx.org/en/docs/http/ngx_http_sub_module.html

