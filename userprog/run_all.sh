#cd ../examples &&
#make clean &&
#make &&
#cd ../userprog &&
#make clean &&
make &&
cd build && 
#pintos --filesys-size=20 -p tests/userprog/args-many -a args-many -- -f -q extract run 'args-many a b c d e f g h i j k l m n o p q r s t u v ' &&
pintos --filesys-size=20 -p ../../tests/userprog/sample.txt -a sample.txt -p tests/userprog/read-normal -a read-normal -- -f -q extract run 'read-normal' &&
cd ..

