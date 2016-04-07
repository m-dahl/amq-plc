////////////////////////////////////////////////////////////////////////////////
// ABSTRACT AWAY APACHE STUFF
////////////////////////////////////////////////////////////////////////////////

#include <activemq/library/ActiveMQCPP.h>
#include <decaf/lang/Thread.h>
#include <decaf/lang/Runnable.h>
#include <decaf/lang/Integer.h>
#include <decaf/lang/Long.h>
#include <decaf/lang/System.h>
#include <decaf/util/concurrent/Mutex.h>
#include <activemq/core/ActiveMQConnectionFactory.h>
#include <activemq/util/Config.h>
#include <cms/Connection.h>
#include <cms/Session.h>
#include <cms/TextMessage.h>
#include <cms/BytesMessage.h>
#include <cms/MapMessage.h>
#include <cms/ExceptionListener.h>
#include <cms/MessageListener.h>

#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <memory>

#include "amq.h"

using namespace activemq::core;
using namespace decaf::util::concurrent;
using namespace decaf::util;
using namespace decaf::lang;
using namespace cms;
using namespace std;

class producer {
private:
	Connection* connection;
	Session* session;
	Destination* destination;
	MessageProducer* mproducer;
	int numMessages;
	bool sessionTransacted;
	std::string brokerURI;
  std::string topic;

private:

	producer(const producer&);
	producer& operator=(const producer&);

public:

	producer(const std::string& brokerURI, const std::string& topic, bool sessionTransacted = false) :
		connection(NULL),
		session(NULL),
		destination(NULL),
		mproducer(NULL),
		sessionTransacted(sessionTransacted),
		brokerURI(brokerURI),
    topic(topic) {
	}

	virtual ~producer(){
		cleanup();
	}

	void close() {
		this->cleanup();
	}

  void setup() {
		try {

			// Create a ConnectionFactory
			auto_ptr<ConnectionFactory> connectionFactory(
				ConnectionFactory::createCMSConnectionFactory(brokerURI));

			// Create a Connection
			connection = connectionFactory->createConnection();
			connection->start();

			// Create a Session
			if (this->sessionTransacted) {
				session = connection->createSession(Session::SESSION_TRANSACTED);
			}
			else {
				session = connection->createSession(Session::AUTO_ACKNOWLEDGE);
			}

			// Create the destination (Topic or Queue)
      destination = session->createTopic(topic);

			// Create a MessageProducer from the Session to the Topic or Queue
			mproducer = session->createProducer(destination);
			mproducer->setDeliveryMode(DeliveryMode::NON_PERSISTENT);
    } catch (CMSException& e) {
			e.printStackTrace();
		}
  }

  void send(const std::string& text) {
    try {
      std::auto_ptr<TextMessage> message(session->createTextMessage(text));
      mproducer->send(message.get());
    }	catch (CMSException& e) {
			e.printStackTrace();
		}
  }

private:

	void cleanup() {

		if (connection != NULL) {
			try {
				connection->close();
			}
			catch (cms::CMSException& ex) {
				ex.printStackTrace();
			}
		}

		// Destroy resources.
		try {
			delete destination;
			destination = NULL;
			delete mproducer;
			mproducer = NULL;
			delete session;
			session = NULL;
			delete connection;
			connection = NULL;
		}
		catch (CMSException& e) {
			e.printStackTrace();
		}
	}
};

class consumer : public ExceptionListener,
                 public MessageListener,
                 public Runnable {

private:

	Connection* connection;
	Session* session;
	Destination* destination;
	MessageConsumer* mconsumer;
  producer *prod;
	bool useTopic;
	bool sessionTransacted;
	std::string brokerURI;
  bool running;
  amq_msg_func msg_func;
  amq_poll_func poll_func;
  std::string topic;
  Mutex* mtx;

private:

	consumer(const consumer&);
	consumer& operator=(const consumer&);

public:

	consumer(const std::string& brokerURI, const std::string& topic, amq_msg_func msg_func,
           amq_poll_func poll_func, bool sessionTransacted = false) :
		connection(NULL),
		session(NULL),
		destination(NULL),
		mconsumer(NULL),
    msg_func(msg_func),
    poll_func(poll_func),
		sessionTransacted(sessionTransacted),
    running(true),
    topic(topic),
    brokerURI(brokerURI),
    mtx(NULL) {
    mtx = new Mutex("snap7");
	}

	virtual ~consumer() {
		cleanup();
    delete mtx;
	}

	void close() {
		this->cleanup();
	}

	virtual void run() {

		try {

			// Create a ConnectionFactory
			auto_ptr<ConnectionFactory> connectionFactory(
				ConnectionFactory::createCMSConnectionFactory(brokerURI));

			// Create a Connection
			connection = connectionFactory->createConnection();
			connection->start();
			connection->setExceptionListener(this);

			// Create a Session
			if (this->sessionTransacted == true) {
				session = connection->createSession(Session::SESSION_TRANSACTED);
			}
			else {
				session = connection->createSession(Session::AUTO_ACKNOWLEDGE);
			}

      destination = session->createTopic(topic);

			// Create a MessageConsumer from the Session to the Topic or Queue
			mconsumer = session->createConsumer(destination);

			mconsumer->setMessageListener(this);

			std::cout.flush();
			std::cerr.flush();

      while(running) {
        mtx->lock();
        poll_func();
        mtx->unlock();
        Thread::sleep(100);
      }
      running = true;

		}
		catch (CMSException& e) {
			e.printStackTrace();
		}
	}

  virtual void stop() {
    running = false;
  }

	// Called from the consumer since this class is a registered MessageListener.
	virtual void onMessage(const Message* message) {
		try {
			const TextMessage* textMessage = dynamic_cast<const TextMessage*> (message);

			if (textMessage != NULL) {
        std::string msg_ascii = textMessage->getText();
        std::wstring msg(msg_ascii.begin(),msg_ascii.end());
        mtx->lock();
        msg_func(msg);
        mtx->unlock();
			} else {
        printf("Did not receive text message...\n");
      }
         
		}
		catch (CMSException& e) {
			e.printStackTrace();
		}

		// Commit all messages.
		if (this->sessionTransacted) {
			session->commit();
		}
	}

	// If something bad happens you see it here as this class is also been
	// registered as an ExceptionListener with the connection.
	virtual void onException(const CMSException& ex AMQCPP_UNUSED) {
		printf("CMS Exception occurred.  Shutting down client.\n");
		ex.printStackTrace();
			std::cout.flush();
			std::cerr.flush();
    
		exit(1);
	}

private:

	void cleanup() {
    printf("cleanup\n");
		if (connection != NULL) {
			try {
				connection->close();
			}
			catch (cms::CMSException& ex) {
				ex.printStackTrace();
			}
		}

		// Destroy resources.
		try {
			delete destination;
			destination = NULL;
			delete mconsumer;
			mconsumer = NULL;
			delete session;
			session = NULL;
			delete connection;
			connection = NULL;
		}
		catch (CMSException& e) {
			e.printStackTrace();
		}
	}
};


static producer *response_p = NULL;
static producer *status_p = NULL;
static consumer *c = NULL;
static Thread *t = NULL;
static bool amq_started = false;

int amq_init(const std::wstring& uri, amq_msg_func msg_func,
             amq_poll_func poll_func) {
  std::string uri_ascii(uri.begin(),uri.end());
  activemq::library::ActiveMQCPP::initializeLibrary();
  amq_started = true;
  response_p = new producer(uri_ascii,"response");
  response_p->setup();
  status_p = new producer(uri_ascii,"status");
  status_p->setup();
  c = new consumer(uri_ascii, "commands", msg_func, poll_func);
  //t = new Thread(c);
  //t->start();
  return 0;
}

void amq_send(const std::wstring& message) {
  std::string msg_ascii(message.begin(),message.end());
  if(response_p) response_p->send(msg_ascii);
}

void amq_status(const std::wstring& message) {
  std::string msg_ascii(message.begin(),message.end());
  if(status_p) status_p->send(msg_ascii);
}

void amq_stop() {
  if(c) c->stop();
}

void amq_mainloop() {
  if(c) c->run();
}

void amq_waitforfinish() {
  if(t) t->join();
}

int amq_shutdown() {
  amq_stop();
  if(t) {
    t->join(); // wait for client to stop
    delete t;
    t = NULL;
  }
  if(c) {
    delete c;
    c = NULL;    
  }
  if(response_p) {
    delete response_p;
    response_p = NULL;
  }
  if(status_p) {
    delete status_p;
    status_p = NULL;
  }
  if(amq_started) {
    activemq::library::ActiveMQCPP::shutdownLibrary();
    amq_started = false;
  }
  return 0;
}
