ARG BASE
FROM ${BASE}

ENV debug=0
ENV target=None
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get -y update \
    && apt-get -y install \
    iproute2 \
    iputils-ping \
    python3-pip \
    ntp \
    time
RUN pip3 install pyyaml \
    && pip3 install redfish

COPY ./rvmc.py /usr/local/bin
ENTRYPOINT /usr/local/bin/rvmc.py --target $target --debug $debug
