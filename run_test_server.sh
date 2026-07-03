#!/bin/bash

# Check if server_token is provided
if [ -z "$1" ]; then
    echo "Usage: ./run_test_server.sh <server_token>"
    echo "Example: ./run_test_server.sh ST_xxxxxxxxxxx"
    exit 1
fi

SERVER_TOKEN=$1
PAYLOAD=${2:-"payload"}

echo "Starting WeChat VoIP Cloud Server Demo..."
echo "APPID: wx769bf6a5775ba85e"
echo "DEVICE ID: RD2600000001"
echo "MODEL ID: YiROQwsClOLubDM-ej_isQ"
echo "PAYLOAD: $PAYLOAD"
echo "SERVER TOKEN: $SERVER_TOKEN"

# Execute the demo with matching parameters
LD_LIBRARY_PATH=../../server/D5100032/lib/ ./voipcloud_demo \
	--appid wx769bf6a5775ba85e \
	--device_id RD2600000001 \
	--model_id YiROQwsClOLubDM-ej_isQ \
	--server_token $SERVER_TOKEN \
	--payload $PAYLOAD
