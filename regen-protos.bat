# the nanopb tool seems to require that the .options file be in the current directory!
SETLOCAL
cd protobufs
C:\Work\tools\nanopb-0.4.9.1\generator-bin\protoc.exe --experimental_allow_proto3_optional "--nanopb_out=-S.cpp -v:../main" -I=../protobufs/ ../protobufs/meshtastic/*.proto
ENDLOCAL