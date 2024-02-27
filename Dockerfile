FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt update
RUN apt update && apt install -y git gettext gcc libkrb5-dev libz-dev libreadline-dev bison flex make libxml2-utils docbook xsltproc docbook-xsl docbook-xml
WORKDIR /
RUN git clone https://git.postgresql.org/git/postgresql.git
WORKDIR /postgresql
RUN git checkout REL_16_0
RUN ./configure --without-icu 
RUN make install-world
WORKDIR /
RUN git clone https://github.com/postgrespro/pg_probackup.git
WORKDIR /pg_probackup
RUN git checkout 2.5.13
RUN make USE_PGXS=1 PG_CONFIG=/usr/local/pgsql/bin/pg_config top_srcdir=/postgresql/ clean install
WORKDIR /
RUN apt install -y libfuse-dev
RUN git clone https://github.com/fukanchik/pg0.git
WORKDIR /pg0
RUN make
WORKDIR /
RUN apt install -y sudo
RUN adduser --disabled-password --gecos "" postgres
RUN sudo -u postgres echo 'export PATH=/usr/local/pgsql/bin:$PATH' >> /home/postgres/.profile
WORKDIR /
RUN apt install -y fuse
COPY demo.sh .
ENTRYPOINT sudo -iu postgres /bin/bash /demo.sh
