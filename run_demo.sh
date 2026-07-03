#!/bin/bash

LD_LIBRARY_PATH=../../server/D5100032/lib/ ./voipcloud_demo \
	--appid appid \
	--device_id sn \
	--model_id model_id \
	--server_token server_token \
	--payload hello
