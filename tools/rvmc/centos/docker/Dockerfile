ARG BASE
FROM ${BASE}
MAINTAINER eric.macdonald@windriver.com
RUN yum install -y iproute python3 pip3 datetime time
RUN pip3 install pyyaml redfish
COPY rvmc.py /usr/local/bin
ENV debug=0
ENV target=None
ENTRYPOINT /usr/local/bin/rvmc.py --target $target --debug $debug

# Execution Examples:
#
#  sudo docker run -v /etc/rvmc.yaml:/etc/rvmc.yaml rvmc:latest
#  sudo docker run -v /etc/rvmc.yaml:/etc/rvmc.yaml --env target=dcloud1 rvmc:latest
#  sudo docker run -v /etc/rvmc.yaml:/etc/rvmc.yaml --env target=dcloud1,dcloud2 rvmc:latest
#  sudo docker run -v /etc/rvmc.yaml:/etc/rvmc.yaml --env target=dcloud1,dcloud2 --env debug=1 rvmc:latest