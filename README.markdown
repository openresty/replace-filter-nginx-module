Name
====

ngx_replace_filter - Streaming regular expression replacement in response bodies.

*This module is not distributed with the Nginx source.* See [the installation instructions](#installation).

Status
======

This module is already quite usable though still at the early phase of development
and is considered experimental.

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

    location /a {
        # proxy_pass/fastcgi_pass/root/...

        # remove line-leading spaces and line-trailing spaces,
        # as well as blank lines:
        replace_filter '^\s+|\s+$' '' g;
    }

    location /b {
        # proxy_pass/fastcgi_pass/root/...

        # only remove line-leading spaces and line-trailing spaces:
        replace_filter '^[ \f\t]+|[ \f\t]+$' '' g;
    }

    location ~ '\.cpp$' {
        # proxy_pass/fastcgi_pass/root/...

        replace_filter_types text/plain;

        # skip C/C++ string literals:
        replace_filter "'(?:\\\\[^\n]|[^'\n])*'" $& g;
        replace_filter '"(?:\\\\[^\n]|[^"\n])*"' $& g;

        # remove all those ugly C/C++ comments:
        replace_filter '/\*.*?\*/|//[^\n]*' '' g;
    }

Description
===========

This Nginx output filter module tries to do regular expression substitions in
a non-buffered manner wherever possible.

This module does *not* use traditional backtracking regular expression engines like PCRE, rather,
it uses the new [sregex](https://github.com/agentzh/sregex) library implemented by the author himself, which was designed with streaming processing in mind from the very beginning:

A good common subset of Perl 5 regular expressions is supported by `sregex`. For the complete
feature list, check out sregex's documentation:

https://github.com/agentzh/sregex#syntax-supported

Response body data is only buffered when absolutely necessary, like facing an incomplete capture that belongs to a possible match near the data chunk boundaries.

Directives
==========

replace_filter
--------------
**syntax:** *replace_filter &lt;regex&gt; &lt;replace&gt;*

**syntax:** *replace_filter &lt;regex&gt; &lt;replace&gt; &lt;options&gt;*

**default:** *no*

**context:** *http, server, location, location if*

**phase:** *output body filter*

Specifies the regex pattern and text to be replaced, with an optional regex flags.

By default, the filter topped matching after the first match is found. This behavior can be changed by specifying the `g` regex option.

The following regex options are supported:

* `g`
: for global search and substituion (default off)
* `i`
: for case-insensitive matching (default off)

Multiple options can be combined in a single string argument, for example:

    replace_filter hello hiya ig;

Nginx variables can be interpolated into the text to be replaced, for example:

    replace_filter \w+ "[$foo,$bar]";

If you want to use the literal dollar sign character (`$`), use the `$$` sequence for that,
for instance:

    replace_filter \w "$$";

Use of submatch capturing variables like `$&`, `$1`, `$2`, and etc are also supported, for example,

    replace_filter [bc]|d [$&-$1-$2] g;

The semantics of the submatch capturing variables is exactly the same as in the Perl 5 language.

Multiple `replace_filter` directives in the same scope is also supported.
All the patterns will be applied at the same time as in a tokenizer.
We will *not* use the longest token match semantics, but rather, patterns will be prioritized according to their order in
the configure file.

Here is an example for removing all the C/C++ comments from a C/C++ source code file:

    replace_filter "'(?:\\\\[^\n]|[^'\n])*'" $& g;
    replace_filter '"(?:\\\\[^\n]|[^"\n])*"' $& g;
    replace_filter '/\*.*?\*/|//[^\n]*' '' g;

When the `Content-Encoding` response header is not empty (like `gzip`), the response
body will always remain intact. So usually you want to disable the gzip compression
in your backend servers' responses by adding the following line to your `nginx.conf`
if you are the ngx_proxy module:

    proxy_set_header Accept-Encoding '';

Your responses can still be gzip compressed on the Nginx server level though.

replace_filter_types
--------------------

**syntax:** *replace_filter_types &lt;mime-type&gt; ...*

**default:** *replace_filter_types text/html*

**context:** *http, server, location, location if*

**phase:** *output body filter*

Specify one or more MIME types (in the `Content-Type` response header) to be processed.

By default, only `text/html` typed responses are processed.

replace_filter_max_buffered_size
---------------------------------
**syntax:** *replace_filter_max_buffered_size &lt;size&gt;*

**default:** *replace_filter_max_buffered_size 8k*

**context:** *http, server, location, location if*

**phase:** *output body filter*

Limits the total size of the data buffered by the module at runtime. Default to `8k`.

When the limit is reached, `replace_filter` will immediately stop processing and
leave all the remaining response body data intact.

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

Trouble Shooting
================

* If you are seeing the error "error while loading shared libraries: libsregex.so.0: cannot open shared object file: No such file or directory"
while starting nginx, then it means that the installation path of your libsregex library
is not in your system's default library search path. You can solve this issue by passing the option `--with-ld-opt='-Wl,-rpath,/usr/local/lib'` to nginx's `./configure` command. Alternatively, you can just add the path of your libsregex.so.0 to the `LD_LIBRARY_PATH` environment value before starting your nginx server.

TODO
====

* optimize the special case for verbatim substitutions, i.e., `replace_filter <regex> $&;`.
* implement the `replace_filter_skip $var` directive to control whether to enable the filter on the fly.
* reduce the amount of data that has to be buffered for when an partial match is already found.
* recycle the memory blocks used to buffer the pending capture data and "complex values" for replacement.
* allow use of inlined Lua code as the `replacement` argument of the `replace_filter` directive to generate the text to be replaced on-the-fly.

Community
=========

English Mailing List
--------------------

The [openresty-en](https://groups.google.com/group/openresty-en) mailing list is for English speakers.

Chinese Mailing List
--------------------

The [openresty](https://groups.google.com/group/openresty) mailing list is for Chinese speakers.

Bugs and Patches
================

Please submit bug reports, wishlists, or patches by

1. creating a ticket on the [GitHub Issue Tracker](http://github.com/agentzh/replace-filter-nginx-module/issues),
1. or posting to the [OpenResty community](http://wiki.nginx.org/HttpLuaModule#Community).

Author
======

Yichun "agentzh" Zhang (章亦春) <agentzh@gmail.com>, CloudFlare Inc.

Copyright and License
=====================

This module is licensed under the BSD license.

Copyright (C) 2012, 2013, by Yichun "agentzh" Zhang (章亦春), CloudFlare Inc.

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

* Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

* Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

See Also
========

* agentzh's sregex library: https://github.com/agentzh/sregex
* The standard ngx_sub_filter module: http://nginx.org/en/docs/http/ngx_http_sub_module.html
* Slides for my talk "sregex: matching Perl 5 regexes on data streams": http://agentzh.org/misc/slides/yapc-na-2013-sregex.pdf
