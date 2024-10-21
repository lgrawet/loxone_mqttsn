// MQTT-SN broker settings
#define MQTTSN_GW_URL "/dev/udp/192.168.10.56/1883"
#define CLIENT_ID "LoxoneMQTTSNClient_Subscriber"

// Loxone port settings
#define LISTEN_PORT "/dev/udp//9902"
#define PUBLISH_PORT "/dev/udp/127.0.0.1/9903"

// Stream reading/writing timeout, in seconds
#define MQTTSN_GW_TIMEOUT 1
#define MQTTSN_GW_MSG_TIMEOUT 1
#define MQTTSN_GW_CONN_TIMEOUT 10

// Message type
#define MQTTSN_TYPE_CONNECT       (0x04)
#define MQTTSN_TYPE_REGISTER      (0x0A)
#define MQTTSN_TYPE_REGACK        (0x0B)
#define MQTTSN_TYPE_PUBLISH       (0x0C)
#define MQTTSN_TYPE_PUBACK        (0x0D)
#define MQTTSN_TYPE_SUBSCRIBE     (0x12)
#define MQTTSN_TYPE_SUBACK        (0x13)
#define MQTTSN_TYPE_PINGREQ       (0x16)
#define MQTTSN_TYPE_PINGRESP      (0x17)
#define MQTTSN_TYPE_DISCONNECT    (0x18)

// Heartbeat topic
#define HEARTBEAT_TOPIC "loxone/mqttsn/subscriber/heartbeat"

// Max buffer size
#define BUFF_SIZE 1000

// Sizing
#define MAX_TOPICS 50
#define MAX_TOPIC_SIZE 50

// Topics vars
int gRegisteredTopics = 0;
int force_reconnect = 0;
char *gTopics[MAX_TOPICS];
int gTopicsIDs[MAX_TOPICS];

// Prepare stream to publish data received from MQTT-SN GW
STREAM *pLoxoneInStream, *pLoxoneOutStream;

// Wait a bit before opening stream, it seems that Loxone sometimes ignore the request if it comes too early
sleep(500);
pLoxoneOutStream = stream_create(PUBLISH_PORT,0,0);// create udp stream
pLoxoneInStream = stream_create(LISTEN_PORT,0,0);// create udp stream

// PicoC on Loxone does not support 2 dimensions arrays :(
int k;
for (k=0; k<MAX_TOPICS; k++) {
  gTopics[k] = malloc (MAX_TOPIC_SIZE);
}

// MQTT-SN stream, will be created in connect() function
STREAM* pMQTTSNStream;

int checkRegisteredTopic(char *topic) {
	int i;
	int topicID = -1;

	// Check if topic is registered
	for (i=0; i<gRegisteredTopics; i++) {
		if (strcmp (topic, gTopics[i]) == 0) {
			// Topic already registered
			topicID = gTopicsIDs[i];
			return topicID;
		}
	}
	return 0;
}

// Register topic ID
int registerTopic (int topicID, char *topic) {
	int registeredTopicID;

	// Check if topic is registered
	registeredTopicID = checkRegisteredTopic(topic);
	if (registeredTopicID) {
		return registeredTopicID;
	}

	strcpy (&gTopics[gRegisteredTopics][0], topic);
	gTopicsIDs[gRegisteredTopics] = topicID;
	gRegisteredTopics++;
	return topicID;
}

// Get topic ID, and register it if it does not exist
int getTopicID (char *topic) {

	char szBuffer[BUFF_SIZE], szBufferIn[BUFF_SIZE];
	int nCnt;
	int i;
	int topicID;
	char status[300];

	// Check if topic is registered
	topicID = checkRegisteredTopic(topic);
	if (topicID) {
		return topicID;
	}

	// Topic not registered, register it
	i = 1;
	// We'll update message length afterwards
	// TODO: handle length > 255
	szBuffer[i++] = MQTTSN_TYPE_REGISTER; // Register
	szBuffer[i++] = 0x00; // TopicID - 0
	szBuffer[i++] = 0x00; // TopicID - 0
	szBuffer[i++] = 0x00; // MsgID - 0
	szBuffer[i++] = 0x01; // MsgID - 1
	strcpy(&szBuffer[i], topic);
	szBuffer[0] = i + strlen(topic);
	// Write to output buffer
	stream_write (pMQTTSNStream, szBuffer, szBuffer[0]);
	stream_flush (pMQTTSNStream);

	// Wait for answer
	szBufferIn = processReceivedMessage(MQTTSN_TYPE_REGACK);
	if (szBufferIn[6]) {
		// Return code != 0, topic not registered
		return -1;
	}

	// OK, topic registered on MQTT-SN Gateway
	topicID = (szBufferIn[2] << 8) + szBufferIn[3];
	registerTopic(topicID, topic);
	return topicID;
}

// Keepalive function
int keepalive() {

	char szBuffer[3], szBufferIn[BUFF_SIZE];
	int nCnt;
	szBuffer[0] = 0x02; // Length
	szBuffer[1] = MQTTSN_TYPE_PINGREQ; // Ping

	// Send Keepalive message
	stream_write (pMQTTSNStream, szBuffer, 2);
	stream_flush (pMQTTSNStream);
	// Wait for answer
	nCnt = stream_read(pMQTTSNStream,szBufferIn,BUFF_SIZE,MQTTSN_GW_TIMEOUT*1000);
	if (nCnt == 0)
		return -1;
	return 1;

}

// Connect function
int connect() {

	char szBuffer[BUFF_SIZE], szBufferIn[BUFF_SIZE];
	int duration = 1000;
	int i;
	int nCnt;
	i = 1; // Skip first byte (length), we will fill it later
	// TODO: handle length > 255
	szBuffer[i++] = MQTTSN_TYPE_CONNECT; // MsgType: Connect
	szBuffer[i++] = 0x04; // Flags: set CleanSession to true
	szBuffer[i++] = 0x01; // ProtocolId: 0x01 (only allowed value)
	szBuffer[i++] = duration >> 8;
	szBuffer[i++] = duration & 0xFF;
	strcpy (&szBuffer[i], CLIENT_ID);
	i+= strlen(CLIENT_ID);
	szBuffer[0] = i;

	// Connect to MQTT-SN Gateway
	while (1) {
		pMQTTSNStream = stream_create(MQTTSN_GW_URL,0,0); // create udp stream
		if (pMQTTSNStream != NULL)
		   break;
		// If connection fails, sleep 1s and retry
		sleep (1000);
	}
	// Send Connect message
	stream_write(pMQTTSNStream, szBuffer, i);
	stream_flush(pMQTTSNStream);
	// Wait and read reply from MQTT-SN gateway
	nCnt = stream_read(pMQTTSNStream,szBufferIn,BUFF_SIZE,MQTTSN_GW_TIMEOUT*1000);
	if (nCnt == 0)
	   return -1;
	return 1;
}

// Disconnect function
int disconnect() {

	char szBuffer[3], szBufferIn[BUFF_SIZE];
	int nCnt;

	szBuffer[0] = 0x02; // Length
	szBuffer[1] = MQTTSN_TYPE_DISCONNECT; // Disconnect

	// Connect to MQTT-SN Gateway
	while (1) {
		pMQTTSNStream = stream_create(MQTTSN_GW_URL,0,0); // create udp stream
		if (pMQTTSNStream != NULL)
			break;
		// If connection fails, sleep 1s and retry
		sleep (1000);
	}

	// Send Disconnect message
	stream_write (pMQTTSNStream, szBuffer, 2); // write to output buffer
	stream_flush (pMQTTSNStream);

	// Wait for answer
	nCnt = stream_read(pMQTTSNStream,szBufferIn,BUFF_SIZE,MQTTSN_GW_TIMEOUT*1000); // read stream, will either reply with 0x18 (DISCONNECT OK) or not reply (no ongoing connection), both are ok

	// Close stream
	stream_close (pMQTTSNStream);

	return 1;
}

void publish_heartbeat() {

   int i;
   char payload[2];
   char szBuffer[BUFF_SIZE];
   int topicID;

   // HEARTBEAT_TOPIC
   topicID = getTopicID (HEARTBEAT_TOPIC);
   payload = "1";

   // Prepare message
   i = 1;
   // We'll update message length afterwards
   // TODO: handle length > 255
   szBuffer[i++] = MQTTSN_TYPE_PUBLISH; // Publish
   szBuffer[i++] = 0x00; // Flag
   szBuffer[i++] = (topicID >> 8); // Topic ID
   szBuffer[i++] = (topicID % 256); // Topic ID
   szBuffer[i++] = 0x00; // MsgID
   szBuffer[i++] = 0x01; // MsgID
   strcpy(&szBuffer[i], payload);
   szBuffer[0] = i + strlen(payload);

   stream_write (pMQTTSNStream, szBuffer, szBuffer[0]); // write to output buffer
   stream_flush (pMQTTSNStream);

}

// Process publish message received from MQTT-SN gateway
int processPublishMessage(int nCnt, char *_message) {

	int topic = 0;
	int i;
	int l;

	char message[BUFF_SIZE];
	char status[20 + BUFF_SIZE];

	// Length can be encoded on 1 or 3 bytes
	l = 0;
	if (_message[0] == 0x01)
	  l = 2; // length encoded on 3 bytes

	// Get topic
	topic = (_message[3+l] << 8) + _message[4+l];

	// Get topic name
	for (i=0; i<gRegisteredTopics; i++) {

		if (gTopicsIDs[i] == topic) {

			// Publish the topic + payload message to Loxone listener
			strncpy (message, &_message[7+l], nCnt-7-l);
			sprintf (status, "%s/%s", gTopics[i], message);
			stream_write (pLoxoneOutStream, status, strlen(status)); // write to output buffer
			stream_flush (pLoxoneOutStream);
			// Set status
			setoutputtext (1, status);
			break;
		}
	}
	return 1;
}

// Process register message received from MQTT-SN gateway
int processRegisterMessage(int nCnt, char *_message) {
	int topicID;
	int msgID;
	int i;
	char topic[MAX_TOPIC_SIZE];
	char szBuffer[BUFF_SIZE];
	char status[200];

	topicID = (_message[2] << 8) + _message[3];
    msgID = (_message[4] << 8) + _message[5];
	strncpy (topic, &_message[6], nCnt-6);

	// Check if topic is registered
	if (!checkRegisteredTopic(topic)) {
		registerTopic(topicID, topic);
		sprintf (status, "SUBSCRIBED: %s", topic);
		setoutputtext(1, status);
	}

	// Send Regack
	i = 1;
	// We'll update message length afterwards
	szBuffer[i++] = MQTTSN_TYPE_REGACK; // Regack
	szBuffer[i++] = (topicID >> 8); // TopicID - 0
	szBuffer[i++] = (topicID & 0xFF); // TopicID - 1
	szBuffer[i++] = (msgID >> 8); // MsgID - 0
	szBuffer[i++] = (msgID & 0xFF); // MsgID - 1
	szBuffer[i++] = 0x00; // Status: accepted
	szBuffer[0] = i;
	// Write to output buffer
	stream_write (pMQTTSNStream, szBuffer, szBuffer[0]);
	stream_flush (pMQTTSNStream);

	return 1;
}

// Process message received from MQTT-SN gateway
char * processReceivedMessage(int msgType) {
	int ct, ct2;
	int l;
    int nct;
	char szBufferIn[BUFF_SIZE];
	char status[300];

	ct = getcurrenttime();

	while (1) {
		// Process data received from MQTT-SN gateway (should be Publish messages)
		// If no data received for 25 seconds, send a keepalive
		// If keepalive fails, restart connection
		setoutputtext (2, "");
		ct2 = getcurrenttime();
		if (ct2 - ct > 30) {
			ct = ct2;
			// Publish heartbeat
			publish_heartbeat ();
		}
		nCnt = stream_read(pMQTTSNStream,szBufferIn,BUFF_SIZE,25000);
		if (nCnt > 0) {
			setoutputtext (1, "");
			szBufferIn[nCnt] = '\0';

			// Length can be encoded on 1 or 3 bytes
			l = 0;
			if (szBufferIn[0] == 0x01)
				l = 2; // length encoded on 3 bytes

			// Skip PINGRESP (not expected but can happen)
			switch (szBufferIn[1+l]) {
				case MQTTSN_TYPE_PINGRESP:
					// Do nothing
					break;
				case MQTTSN_TYPE_PUBLISH:
					processPublishMessage (nCnt, szBufferIn);
					break;
				case MQTTSN_TYPE_PUBACK:
					break;
				case MQTTSN_TYPE_SUBACK:
					break;
				case MQTTSN_TYPE_REGISTER:
					processRegisterMessage (nCnt, szBufferIn);
					break;
				case MQTTSN_TYPE_REGACK:
					break;
				default:
					sprintf (status, "Unexpected message: %d %d %d", _message[0+l],_message[1+l],_message[2+l]);
					setoutputtext(1, status);
					break;
			}
			// Did we find what we were looking for?
			if (szBufferIn[1+l] == msgType)
				return &szBufferIn[0];
		} else {
			setoutputtext (2, "KEEPALIVE");
			if (keepalive() == 1) {
				// Keep alive ok
				sleep (10);
			}
			else {
				// Connection dead, reconnect
				setoutputtext(0,"CONNECTION DEAD");
				force_reconnect = 1;
				break;
			}
		}
	}
	return NULL;
}

// Process subscription message received from Loxone
int processSubscriptionRequest(int nCnt, char *message) {

	int i;
	char* pos;
	char topic[MAX_TOPIC_SIZE];
	char payload[200];
	char szBuffer[BUFF_SIZE];
	int topicID;
	char status[200];

	topic = &message[0];

	// Check if topic is registered
	topicID = checkRegisteredTopic(topic);
	if (topicID) {
		return 1;
	}

	// Prepare message
	i = 1;
	// We'll update message length afterwards
	// TODO: handle length > 255
	szBuffer[i++] = MQTTSN_TYPE_SUBSCRIBE; // Subscribe
	szBuffer[i++] = 0x00; // Flag: topic name
	szBuffer[i++] = 0x20; // MsgID (let's use 0x20 for Subscribe Messages)
	szBuffer[i++] = 0x00; // MsgID
	strcpy(&szBuffer[i], topic);
	szBuffer[0] = i + strlen(topic);
	stream_write (pMQTTSNStream, szBuffer, szBuffer[0]); // write to output buffer
	stream_flush (pMQTTSNStream);

	// Wait for answer
	szBufferIn = processReceivedMessage(MQTTSN_TYPE_SUBACK);
	if (szBufferIn[7]) {
		// Return code != 0, topic not subscribed
		return -1;
	}
	else {
		// OK, topic subscribed on MQTT-SN Gateway
		topicID = (szBufferIn[3] << 8) + szBufferIn[4];
	}

	if (topicID) {
		registerTopic (topicID, topic);
		sprintf (status, "SUBSCRIBED: %s", topic);
		setoutputtext(1, status);
	}

	return 1;
}

// Main

// Flush pending connection, if any
disconnect();
int nCnt;
char szBufferIn[BUFF_SIZE];
char status[300];

force_reconnect = 0;

while (1) {

	// Connect
	while (1) {
        setoutputtext (0, "CONNECTING");
		if (connect() == -1) {
			setoutputtext(0,"Connection failed");
			sleep (MQTTSN_GW_CONN_TIMEOUT);
		}
		else {
			setoutputtext(0,"CONNECTED");
			gRegisteredTopics = 0;
			// Subscribe topics by sending a pulse on Output 13
			setoutput (12, 1);
            sleep (300);
            setoutput (12, 0);
            force_reconnect = 0;
			break;
		}
	}

	while (force_reconnect == 0) {
		// Process all subscription requests
		while (1) {
			nCnt = stream_read(pLoxoneInStream,szBufferIn,BUFF_SIZE,5000);
			if (nCnt > 0) {
				szBufferIn[nCnt] = 0;
				processSubscriptionRequest(nCnt, szBufferIn);
                sleep (10);
			} else {
				break;
			}
		}
		sleep (50);
		while (1) {
			processReceivedMessage(MQTTSN_TYPE_PUBLISH);
		}
	}
}
