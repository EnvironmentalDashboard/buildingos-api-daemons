FROM ubuntu
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=America/New_York
# timezone: https://serverfault.com/a/683651/456938
RUN apt-get update && \
  apt-get -qq -y install apt-utils build-essential tzdata libcurl4-openssl-dev libmysqlclient-dev rsyslog logrotate man gdb && \
  ln -snf /usr/share/zoneinfo/$TZ /etc/localtime && echo $TZ > /etc/timezone
COPY . /src
WORKDIR /src
RUN make all
CMD ["service rsyslog start && /src/buildingosd"]
