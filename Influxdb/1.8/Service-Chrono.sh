#!/bin/bash
tar xzf chronograf.tar.gz
cp -r chronograf ~/
cp -r chronograf.sh ~/
sudo cp -r chronograf.service /etc/systemd/system/
sudo systemctl daemon-reexec
sudo systemctl daemon-reload
sudo systemctl enable Chronograf.service
sudo systemctl start Chronograf.service
