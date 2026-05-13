FROM ubuntu:latest

WORKDIR /app

COPY server /app/server
RUN chmod +x /app/server && mkdir -p /app/db

EXPOSE 9000

CMD ["./server"]
