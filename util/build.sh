#!/bin/bash

# this file is mostly meant to be used by the author himself.

root=`pwd`
home=~
version=$1
force=$2

ngx-build $force $version \
            --with-ld-opt="-Wl,-rpath,$SREGEX_LIB:$LUAJIT_LIB:/usr/local/lib" \
        --with-http_ssl_module \
            --without-mail_pop3_module \
            --without-mail_imap_module \
            --without-mail_smtp_module \
            --without-http_upstream_ip_hash_module \
            --without-http_empty_gif_module \
            --without-http_memcached_module \
            --without-http_referer_module \
            --without-http_autoindex_module \
            --without-http_auth_basic_module \
            --without-http_userid_module \
          --add-module=$root $opts \
          --add-module=$home/git/lua-nginx-module \
          --add-module=$home/git/echo-nginx-module \
          --with-debug
          #--with-cc-opt="-g3 -O0"
          #--add-module=$root/../echo-nginx-module \
  #--without-http_ssi_module  # we cannot disable ssi because echo_location_async depends on it (i dunno why?!)

