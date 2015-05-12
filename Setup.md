# ec2-52-24-129-29.us-west-2.compute.amazonaws.com

## Hostname

* rtb1.uswest2a.jamloop in /etc/hostname
* 127.0.0.1 rtb1.uswest2a.jamloop localhost in /etc/hosts

## System Packages (apt)

* sysstat
* aptitude
* curl
* pv
* tmux
* htop
* bmon
* wget
* traceroute
* unzip
* git-core
* tcpdump
* tcpick
* python-pip
* python-software-properties
* dnsutils
* ipmitool
* freeipmi-tools
* python
* python-dev
* iotop
* python-virtualenv
* apt-file
* bsd-mailx
* rsyslog
* lvm2
* cmake
* man-db
* ntp
* ntpdate
* linux-tools
* libbz2-dev
* python-dev
* scons
* libtool
* liblzma-dev
* libblas-dev
* make
* automake
* ccache
* ant
* openjdk-7-jdk
* libcppunit-dev
* doxygen
* libcrypto++-dev
* libace-dev
* gfortran
* liblapack-dev
* libevent-dev
* libssh2-1-dev
* libicu-dev
* g++
* google-perftools
* libgoogle-perftools-dev
* zlib1g-dev
* git
* pkg-config
* valgrind
* autoconf

* libcairo2-dev (For graphite)
* libffi-dev (For graphite)

## Pip Packages (pip)

* Django==1.6.5
* Twisted
* cairocffi
* cffi
* django-tagging==0.3.1
* pyparsing>=1.5.7
* python-memcached==1.47
* pytz
* simplejson
* txAMQP==0.4
* uWSGI==2.0.10


## Datacratic User

Used by Datacratic staff to access the machine.



## RTBkit User

Used to run the rtbkit specific services.

### Folder

* /home/rtbkit/prod/etc
* /home/rtbkit/prod/var
* /home/rtbkit/workspace
* /home/rtbkit/prod/etc/zookeeper
* /home/rtbkit/prod/var/zookeeper
* /home/rtbkit/prod/var/redis-banker
* /home/rtbkit/prod/var/www/quickboard

### Configuration

* /home/rtbkit/.profile


## Graphite/Carbon

### Folders

* /opt/graphite (chown www-data:www-data)
* /opt/graphite/storage
* /var/log/uwsgi

### Source

* https://github.com/datacratic/carbon.git cloned in /home/rtbkit/workspace/carbon
* https://github.com/datacratic/graphite-web.git cloned in /home/rtbkit/workspace/graphite-web
* https://github.com/datacratic/whisper.git cloned in /home/rtbkit/workspace/whisper
* https://github.com/graphite-project/ceres.git cloned in https://github.com/graphite-project/ceres.git

### Cleanup

* /etc/cron.daily/graphite-whisper-cleanup

### Init Scripts

* /etc/init.d/carbon-aggregator
* /etc/init.d/carbon-cache
* /etc/init.d/graphite

### Log rotation

* /etc/logrotate.d/graphite
* /etc/logrotate.d/uwsgi-all

### Configuration

* /opt/graphite/conf/aggregation-rules.conf
* /opt/graphite/conf/carbon.conf
* /opt/graphite/conf/graphTemplates.conf
* /opt/graphite/conf/graphite.wsgi
* /opt/graphite/conf/storage-aggregation.conf
* /opt/graphite/conf/storage-schemas.conf
* /opt/graphite/webapp/graphite/local_settings.py


## Redis Banker

### Init Scripts
/etc/init.d/redis-server-banker

### Configuration
/home/rtbkit/prod/etc/redis-banker.conf

### Local backups
/var/spool/cron/crontabs/rtbkit
  cp /home/rtbkit/prod/var/redis-banker/dump-banker.rdb /home/rtbkit/prod/var/redis-banker/dump-banker.`date
        +\%y\%m\%d-\%H\%M\%S`.rdb
  at 5 min interval

### Cleanup
/var/spool/cron/crontabs/rtbkit
  find /home/rtbkit/prod/var/redis-banker/ -type f -name 'dump-banker*.rdb'
        -mtime +15 -delete
  at hour 3 every day



## Zookeeper

### Configuration

* /home/rtbkit/prod/etc/zookeeper/zoo.cfg
* /home/rtbkit/prod/etc/zookeeper/log4j.properties

### Init Scripts

* /etc/init/zookeeper.conf





## Quickboard

### Source

* https://github.com/rtbkit/quickboard.git cloned in /home/rtbkit/workspace/quickboard


## Nginx

### PPA

* /etc/apt/sources.list.d/nginx_stable_ppa.list

### Configuration

* /etc/nginx/conf.d/logging.conf
* /etc/nginx/sites-enabled/default (Deleted)
* /etc/nginx/sites-enabled/graphite
* /etc/nginx/sites-enabled/quickboard

### Folders

* /etc/nginx/ssl (For future use)


## RTBkit Dependencies

### Source

* https://github.com/rtbkit/rtbkit-deps.git cloned in /home/rtbkit/workspace/rtbkit-deps


