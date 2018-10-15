FROM ubuntu

COPY cerberus /usr/local/bin/
COPY example.conf /etc/cerberus.conf
COPY docker-entrypoint.sh /usr/local/bin/

EXPOSE 8889

CMD ["docker-entrypoint.sh"]
