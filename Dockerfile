FROM python:3.13-slim

WORKDIR /opt

RUN apt update  \
    && apt install -y --no-install-recommends g++ libreadline-dev libfuse3-dev \
    && rm -rf /var/lib/apt

COPY requirements.txt .

RUN pip install -r requirements.txt

COPY . .

RUN g++ -o kubsh src/main.cpp -lreadline -lfuse3
RUN chmod +x kubsh

RUN mkdir -p /opt/users
ENV VFS_DIR=/opt/users

CMD ["python", "-m", "pytest", "-v"]
