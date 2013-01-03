# vim:set ft= ts=4 sw=4 et fdm=marker:

use lib 'lib';
use Test::Nginx::Socket;

#worker_connections(1014);
#master_on();
#workers(2);
#log_level('warn');

repeat_each(2);

plan tests => repeat_each() * (blocks() * 4);

our $StapOutputChains = <<'_EOC_';
global active

F(ngx_http_handler) {
    active = 1
}

/*
F(ngx_http_write_filter) {
    if (active && pid() == target()) {
        printf("http writer filter: %s\n", ngx_chain_dump($in))
    }
}
*/

F(ngx_http_chunked_body_filter) {
    if (active && pid() == target()) {
        printf("http chunked filter: %s\n", ngx_chain_dump($in))
    }
}

probe syscall.writev {
    if (active && pid() == target()) {
        printf("writev(%s)", ngx_iovec_dump($vec, $vlen))
        /*
        for (i = 0; i < $vlen; i++) {
            printf(" %p [%s]", $vec[i]->iov_base, text_str(user_string_n($vec[i]->iov_base, $vec[i]->iov_len)))
        }
        */
    }
}

probe syscall.writev.return {
    if (active && pid() == target()) {
        printf(" = %s\n", retstr)
    }
}

_EOC_

#no_diff();
#no_long_string();
run_tests();

__DATA__

=== TEST 1: ambiguous pattern
--- config
    default_type text/html;
    location /t {
        echo abcabcabde;
        replace_filter abcabd X;
    }
--- request
GET /t
--- response_body
abcXe
--- no_error_log
[alert]
[error]



=== TEST 2: ambiguous pattern
--- config
    default_type text/html;
    location /t {
        echo -n ababac;
        replace_filter abac X;
    }
--- request
GET /t
--- response_body chop
abX
--- no_error_log
[alert]
[error]



=== TEST 3: alt
--- config
    default_type text/html;
    location /t {
        echo abc;
        replace_filter 'ab|abc' X;
    }
--- request
GET /t
--- response_body
Xc
--- no_error_log
[alert]
[error]



=== TEST 4: caseless
--- config
    default_type text/html;
    location /t {
        echo abcabcaBde;
        replace_filter abCabd X i;
    }
--- request
GET /t
--- response_body
abcXe
--- no_error_log
[alert]
[error]



=== TEST 5: case sensitive (no match)
--- config
    default_type text/html;
    location /t {
        echo abcabcaBde;
        replace_filter abCabd X;
    }
--- request
GET /t
--- response_body
abcabcaBde
--- no_error_log
[alert]
[error]



=== TEST 6: 1-byte chain bufs
--- config
    default_type text/html;

    location = /t {
        echo -n a;
        echo -n b;
        echo -n a;
        echo -n b;
        echo -n a;
        echo -n c;
        echo d;
        replace_filter abac X;
    }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
abXd
--- no_error_log
[alert]
[error]



=== TEST 7: 2-byte chain bufs
--- config
    default_type text/html;

    location = /t {
        echo -n ab;
        echo -n ab;
        echo -n ac;
        echo d;
        replace_filter abac X;
    }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
abXd
--- no_error_log
[alert]
[error]



=== TEST 8: 3-byte chain bufs
--- config
    default_type text/html;

    location = /t {
        echo -n aba;
        echo -n bac;
        echo d;
        replace_filter abac X;
    }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
abXd
--- no_error_log
[alert]
[error]



=== TEST 9: 3-byte chain bufs (more)
--- config
    default_type text/html;

    location = /t {
        echo -n aba;
        echo -n bac;
        echo d;
        replace_filter abacd X;
    }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
abX
--- no_error_log
[alert]
[error]



=== TEST 10: once by default (1st char matched)
--- config
    default_type text/html;
    location /t {
        echo abcabcabde;
        replace_filter a X;
    }
--- request
GET /t
--- response_body
Xbcabcabde
--- no_error_log
[alert]
[error]



=== TEST 11: once by default (2nd char matched)
--- config
    default_type text/html;
    location /t {
        echo abcabcabde;
        replace_filter b X;
    }
--- request
GET /t
--- response_body
aXcabcabde
--- no_error_log
[alert]
[error]



=== TEST 12: global substitution
--- config
    default_type text/html;
    location /t {
        echo bbc;
        replace_filter b X g;
    }
--- request
GET /t
--- response_body
XXc
--- no_error_log
[alert]
[error]



=== TEST 13: global substitution
--- config
    default_type text/html;
    location /t {
        echo abcabcabde;
        replace_filter b X g;
    }
--- request
GET /t
--- response_body
aXcaXcaXde
--- no_error_log
[alert]
[error]



=== TEST 14: global substitution (empty captures)
--- config
    default_type text/html;
    location /t {
        echo -n abcabcabde;
        replace_filter [0-9]* X g;
    }
--- request
GET /t
--- response_body chop
XaXbXcXaXbXcXaXbXdXeX
--- no_error_log
[alert]
[error]



=== TEST 15: global substitution (empty captures, splitted)
--- config
    default_type text/html;
    location /t {
        echo -n ab;
        echo -n cab;
        echo -n c;
        echo -n abde;
        replace_filter [0-9]* X g;
    }
--- request
GET /t
--- response_body chop
XaXbXcXaXbXcXaXbXdXeX
--- no_error_log
[alert]
[error]



=== TEST 16: global substitution (\d+)
--- config
    default_type text/html;
    location /t {
        echo "hello1234, 56 world";
        replace_filter \d+ X g;
    }
--- request
GET /t
--- response_body
helloX, X world
--- no_error_log
[alert]
[error]



=== TEST 17: replace_filter_types default to text/html
--- config
    default_type text/plain;
    location /t {
        echo abc;
        replace_filter b X;
    }
--- request
GET /t
--- response_body
abc
--- no_error_log
[alert]
[error]



=== TEST 18: custom replace_filter_types
--- config
    default_type text/plain;
    location /t {
        echo abc;
        replace_filter b X;
        replace_filter_types text/plain;
    }
--- request
GET /t
--- response_body
aXc
--- no_error_log
[alert]
[error]



=== TEST 19: multiple replace_filter_types settings
--- config
    default_type text/plain;
    location /t {
        echo abc;
        replace_filter b X;
        replace_filter_types text/css text/plain;
    }
--- request
GET /t
--- response_body
aXc
--- no_error_log
[alert]
[error]



=== TEST 20: trim leading spaces
--- config
    default_type text/html;
    location /a.html {
        replace_filter '^\s+' '' g;
    }
--- user_files
>>> a.html
  hello, world  
blah yeah
hello  
   baby!
     
abc
--- request
GET /a.html
--- response_body
hello, world  
blah yeah
hello  
baby!
abc
--- no_error_log
[alert]
[error]



=== TEST 21: trim trailing spaces
--- config
    default_type text/html;
    location /a.html {
        replace_filter '\s+$' '' g;
    }
--- user_files
>>> a.html
  hello, world  
blah yeah
hello  
   baby!
     
abc
--- request
GET /a.html
--- response_body chop
  hello, world
blah yeah
hello
   baby!
abc
--- no_error_log
[alert]
[error]



=== TEST 22: trim both leading and trailing spaces
--- config
    default_type text/html;
    location /a.html {
        replace_filter '^\s+|\s+$' '' g;
    }
--- user_files
>>> a.html
  hello, world  
blah yeah
hello  
   baby!
     
abc
--- request
GET /a.html
--- response_body chop
hello, world
blah yeah
hello
baby!
abc
--- no_error_log
[alert]
[error]



=== TEST 23: pure flush buf in the stream (no data)
--- config
    default_type text/html;
    location = /t {
        echo_flush;
        replace_filter 'a' 'X' g;
    }
--- request
GET /t
--- response_body chop
--- no_error_log
[alert]
[error]



=== TEST 24: pure flush buf in the stream (with data)
--- config
    default_type text/html;
    location = /t {
        echo a;
        echo_flush;
        replace_filter 'a' 'X' g;
    }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
X
--- no_error_log
[alert]
[error]



=== TEST 25: trim both leading and trailing spaces (1 byte at a time)
--- config
    default_type text/html;
    location = /t {
        echo -n 'a';
        echo ' ';
        echo "b";
        replace_filter '^\s+|\s+$' '' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body chop
a
b

--- no_error_log
[alert]
[error]



=== TEST 26: trim both leading and trailing spaces (1 byte at a time), no \s for $
--- config
    default_type text/html;
    location = /t {
        echo -n 'a';
        echo ' ';
        echo "b";
        replace_filter '^\s+| +$' '' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
a
b

--- no_error_log
[alert]
[error]



=== TEST 27: trim both leading and trailing spaces (1 byte at a time)
--- config
    default_type text/html;
    location /a.html {
        internal;
    }

    location = /t {
        content_by_lua '
            local res = ngx.location.capture("/a.html")
            local txt = res.body
            for i = 1, string.len(txt) do
                ngx.print(string.sub(txt, i, i))
                ngx.flush(true)
            end
        ';
        replace_filter '^\s+|\s+$' '' g;
    }
--- user_files
>>> a.html
  hello, world  
blah yeah
hello  
   baby!
     
abc

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- request
GET /t
--- response_body chop
hello, world
blah yeah
hello
baby!
abc
--- no_error_log
[alert]
[error]



=== TEST 28: \b at the border
--- config
    default_type text/html;
    location /t {
        echo -n a;
        echo b;
        replace_filter '\bb|a' X g;
    }
--- request
GET /t
--- response_body
Xb
--- no_error_log
[alert]
[error]



=== TEST 29: \B at the border
--- config
    default_type text/html;
    location /t {
        echo -n a;
        echo ',';
        replace_filter '\B,|a' X g;
    }
--- request
GET /t
--- response_body
X,
--- no_error_log
[alert]
[error]



=== TEST 30: \A at the border
--- config
    default_type text/html;
    location /t {
        echo -n a;
        echo 'b';
        replace_filter '\Ab|a' X g;
    }
--- request
GET /t
--- response_body
Xb
--- no_error_log
[alert]
[error]



=== TEST 31: memory bufs with last_buf=1
--- config
    default_type text/html;
    location /t {
        return 200 "abc";
        replace_filter \w+ X;
    }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body chop
X
--- no_error_log
[alert]
[error]



=== TEST 32: trim both leading and trailing spaces (2 bytes at a time)
--- config
    default_type text/html;
    location /a.html {
        internal;
    }

    location = /t {
        content_by_lua '
            local res = ngx.location.capture("/a.html")
            local txt = res.body
            local len = string.len(txt)
            i = 1
            while i <= len do
                if i == len then
                    ngx.print(string.sub(txt, i, i))
                    i = i + 1
                else
                    ngx.print(string.sub(txt, i, i + 1))
                    i = i + 2
                end
                ngx.flush(true)
            end
        ';
        replace_filter '^\s+|\s+$' '' g;
    }
--- user_files
>>> a.html
  hello, world  
blah yeah
hello  
   baby!
     
abc

--- stap2 eval: $::StapOutputChains
--- request
GET /t
--- response_body chop
hello, world
blah yeah
hello
baby!
abc
--- no_error_log
[alert]
[error]



=== TEST 33: trim both leading and trailing spaces (3 bytes at a time)
--- config
    default_type text/html;
    location /a.html {
        internal;
    }

    location = /t {
        content_by_lua '
            local res = ngx.location.capture("/a.html")
            local txt = res.body
            local len = string.len(txt)
            i = 1
            while i <= len do
                if i == len then
                    ngx.print(string.sub(txt, i, i))
                    i = i + 1
                elseif i == len - 1 then
                    ngx.print(string.sub(txt, i, i + 1))
                    i = i + 2
                else
                    ngx.print(string.sub(txt, i, i + 2))
                    i = i + 3
                end
                ngx.flush(true)
            end
        ';
        replace_filter '^\s+|\s+$' '' g;
    }
--- user_files
>>> a.html
  hello, world  
blah yeah
hello  
   baby!
     
abc

--- stap2 eval: $::StapOutputChains
--- request
GET /t
--- response_body chop
hello, world
blah yeah
hello
baby!
abc
--- no_error_log
[alert]
[error]



=== TEST 34: github issue #2: error "general look-ahead not supported"
--- config
    location /t {
         charset utf-8;
         default_type text/html;
         echo "ABCabcABCabc";
         #replace_filter_types text/plain;
         replace_filter "a.+a" "X" "ig";
     }
--- request
GET /t
--- response_body
Xbc
--- no_error_log
[alert]
[error]



=== TEST 35: backtrack to the middle of a pending capture (pending: output|capture + rematch)
--- config
    default_type text/html;
    location = /t {
        echo -n ab;
        echo -n c;
        echo d;
        replace_filter 'abce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcd

--- no_error_log
[alert]
[error]



=== TEST 36: backtrack to the middle of a pending capture (pending: output + capture|rematch
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n bc;
        echo d;
        replace_filter 'abce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcd

--- no_error_log
[alert]
[error]



=== TEST 37: backtrack to the middle of a pending capture (pending: output + capture + rematch
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n b;
        echo -n c;
        echo d;
        replace_filter 'abce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcd

--- no_error_log
[alert]
[error]



=== TEST 38: backtrack to the middle of a pending capture (pending: output|capture|rematch
--- config
    default_type text/html;
    location = /t {
        echo -n abc;
        echo d;
        replace_filter 'abce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcd

--- no_error_log
[alert]
[error]



=== TEST 39: backtrack to the middle of a pending capture (pending: output|capture|rematch(2)
--- config
    default_type text/html;
    location = /t {
        echo -n abcc;
        echo d;
        replace_filter 'abcce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXccd

--- no_error_log
[alert]
[error]



=== TEST 40: backtrack to the middle of a pending capture (pending: output|capture(2)|rematch
--- config
    default_type text/html;
    location = /t {
        echo -n abbc;
        echo d;
        replace_filter 'abbce|bb' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcd

--- no_error_log
[alert]
[error]



=== TEST 41: backtrack to the middle of a pending capture (pending: output(2)|capture|rematch
--- config
    default_type text/html;
    location = /t {
        echo -n aabc;
        echo d;
        replace_filter 'aabce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aaXcd

--- no_error_log
[alert]
[error]



=== TEST 42: backtrack to the beginning of a pending capture (pending: output + capture|rematch(2)
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n bcc;
        echo d;
        replace_filter 'abcce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXccd

--- no_error_log
[alert]
[error]



=== TEST 43: backtrack to the beginning of a pending capture (pending: output + capture(2)|rematch
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n bbc;
        echo d;
        replace_filter 'abbce|bb' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcd

--- no_error_log
[alert]
[error]



=== TEST 44: backtrack to the middle of a pending capture (pending: output(2) + capture|rematch
--- config
    default_type text/html;
    location = /t {
        echo -n aa;
        echo -n bc;
        echo d;
        replace_filter 'aabce|b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aaXcd

--- no_error_log
[alert]
[error]



=== TEST 45: assertions across AGAIN
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n "\n";
        echo b;
        replace_filter 'a\n^b' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
X

--- no_error_log
[alert]
[error]



=== TEST 46: assertions when capture backtracking happens
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n b;
        echo -n c;
        echo -n d;
        echo f;
        #echo abcdf;
        replace_filter 'abcde|b|\bc' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXcdf

--- no_error_log
[alert]
[error]



=== TEST 47: assertions when capture backtracking happens (2 pending matches)
--- config
    default_type text/html;
    location = /t {
        echo -n a;
        echo -n b;
        echo -n ' ';
        echo -n d;
        echo f;
        #echo ab df;
        replace_filter 'ab de|b|b |\b ' 'X' g;
    }

--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- stap3 eval: $::StapOutputChains
--- request
GET /t
--- response_body
aXXdf

--- no_error_log
[alert]
[error]



=== TEST 48: github issue #2: error "general look-ahead not supported", no "g"
--- config
    location /t {
         charset utf-8;
         default_type text/html;
         echo "ABCabcABCabc";
         #replace_filter_types text/plain;
         replace_filter "a.+a" "X" "i";
     }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
Xbc
--- no_error_log
[alert]
[error]



=== TEST 49: nested rematch bufs
--- config
    location /t {
         default_type text/html;
         echo -n a;
         echo -n b;
         echo -n c;
         echo -n d;
         echo -n e;
         echo g;
         #echo abcdeg;
         replace_filter 'abcdef|b|cdf|c' X g;
     }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
aXXdeg
--- no_error_log
[alert]
[error]



=== TEST 50: nested rematch bufs (splitting pending buf)
--- config
    location /t {
         default_type text/html;
         echo -n a;
         echo -n b;
         echo -n cd;
         echo -n e;
         echo -n f;
         echo -n g;
         echo i;
         #echo abcdefh;
         replace_filter 'abcdefgh|b|cdeg|d' X g;
     }
--- request
GET /t
--- stap2 eval: $::StapOutputChains
--- response_body
aXcXefgi
--- no_error_log
[alert]
[error]



=== TEST 51: remove C/C++ comments (1 byte at a time)
--- config
    default_type text/html;
    location /a.html {
        internal;
    }

    location = /t {
        content_by_lua '
            local res = ngx.location.capture("/a.html")
            local txt = res.body
            for i = 1, string.len(txt) do
                ngx.print(string.sub(txt, i, i))
                ngx.flush(true)
            end
        ';
        replace_filter '/\*.*?\*/|//[^\n]*' '' g;
    }
--- user_files
>>> a.html
 i don't know   // hello // world /* */
hello world /** abc * b/c /*
    hello ** // world
    *
    */
blah /* hi */ */ b
//
///hi
--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- request
GET /t
--- response_body eval
" i don't know   
hello world 
blah  */ b


"
--- no_error_log
[alert]
[error]



=== TEST 52: remove C/C++ comments (all at a time)
--- config
    default_type text/html;

    location /a.html {
        replace_filter '/\*.*?\*/|//[^\n]*' '' g;
    }

--- user_files
>>> a.html
 i don't know   // hello // world /* */
hello world /** abc * b/c /*
    hello ** // world
    *
    */
blah /* hi */ */ b
//
///hi
--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- request
GET /a.html
--- response_body eval
" i don't know   
hello world 
blah  */ b


"
--- no_error_log
[alert]
[error]



=== TEST 53: remove C/C++ comments (all at a time) - server-level config
--- config
    default_type text/html;

    replace_filter '/\*.*?\*/|//[^\n]*' '' g;

--- user_files
>>> a.html
 i don't know   // hello // world /* */
hello world /** abc * b/c /*
    hello ** // world
    *
    */
blah /* hi */ */ b
//
///hi
--- stap2
F(ngx_palloc) {
    if ($size < 0) {
        print_ubacktrace()
        exit()
    }
}
--- request
GET /a.html
--- response_body eval
" i don't know   
hello world 
blah  */ b


"
--- no_error_log
[alert]
[error]



=== TEST 54: multiple replace_filter_types settings (server level)
--- config
    default_type text/plain;
    replace_filter_types text/css text/plain;
    location /t {
        echo abc;
        replace_filter b X;
    }
--- request
GET /t
--- response_body
aXc
--- no_error_log
[alert]
[error]



=== TEST 55: multiple replace_filter_types settings (server level, but overridding in location)
--- config
    default_type text/plain;
    replace_filter_types text/css text/plain;
    location /t {
        echo abc;
        replace_filter_types text/html;
        replace_filter b X;
    }
--- request
GET /t
--- response_body
abc
--- no_error_log
[alert]
[error]



=== TEST 56: do not use replace_filter at all
--- config
    default_type text/plain;
    replace_filter_types text/css text/plain;
    location /t {
        echo abc;
        replace_filter_types text/css;
    }
--- request
GET /t
--- response_body
abc
--- no_error_log
[alert]
[error]



=== TEST 57: bad regex
--- config
    default_type text/html;
    location /t {
        echo abc;
        replace_filter '(a+b' '';
    }
--- request
GET /t
--- response_body
abc
--- no_error_log
[alert]
[error]
--- SKIP

