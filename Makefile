
main: main.cpp amq.cpp amq.h
	g++ -g3 -o main main.cpp amq.cpp snap7.cpp JSONValue.cpp JSON.cpp -lsnap7 -lactivemq-cpp -L/usr/local/lib -I/usr/local/include/activemq-cpp-3.9.2 -I/usr/include/apr-1.0

clean:
	rm main
