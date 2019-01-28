FROM centos:7

RUN yum update -y && yum install -y pigz createrepo mock rpmdevtools which deltarpm rpm-sign sudo redhat-lsb-core subversion httpd gcc-c++ gperf make rpm-build glibc-devel at curl curl-devel expat-devel gettext-devel openssl-devel zlib-devel libevent-devel rsync redhat-lsb php php-mbstring autoconf libtool valgrind pcre-devel libuuid-devel python wget git memcached libstdc++-static libstdc++-devel libstdc++-devel.i686 net-tools && yum groupinstall -y 'Development Tools'
RUN useradd -u 1000 -G mock builder
RUN echo "builder ALL=(ALL) NOPASSWD: ALL" >> /etc/sudoers

RUN install -g mock -m 2775 -d /var/cache/mock
RUN echo "config_opts['cache_topdir'] = '/var/cache/mock'" >> /etc/mock/site-defaults.cfg

VOLUME /home/builder/source

RUN yum update -y && yum install -y initscripts httpd httpd-devel

USER builder
ENV USER=builder
ENV HOME=/home/builder
WORKDIR /home/builder/source

ENTRYPOINT ["install/build_release.sh"]
