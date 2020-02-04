ARG BASE
FROM ${BASE}
MAINTAINER eric.macdonald@windriver.com
RUN yum install -y iproute python3-pip-9.0.3-5.el7 datetime time
RUN pip3 install pyyaml redfish
COPY rvmc.py /usr/local/bin
ENV debug=0
ENV target=None
ENTRYPOINT /usr/local/bin/rvmc.py --target $target --debug $debug

