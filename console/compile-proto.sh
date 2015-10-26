echo "build galaxy.proto"
protoc -I ../../../galaxy/src/proto/  --python_out=src/galaxy/ ../../../galaxy/src/proto/galaxy.proto
echo "build master.proto"
protoc -I ../../../galaxu/src/proto/  --python_out=src/galaxy/ ../../../galaxy/src/proto/master.proto
echo "build lumia agent.proto"
protoc -I ../src/proto/  --python_out=src/lumia/ ../../src/proto/agent.proto
protoc -I ../src/proto/  --python_out=src/lumia/ ../../src/proto/lumia.proto
