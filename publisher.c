// MQTT-SN broker settings
#define MQTTSN_GW_URL "/dev/udp/192.168.10.56/1884"
#define CLIENT_ID "LoxoneMQTTSNClient_Publisher"

// Loxone port settings
#define LISTEN_PORT "/dev/udp//9904"

// Stream reading/writing timeout, in seconds
#define MQTTSN_GW_TIMEOUT 1
#define MQTTSN_GW_MSG_TIMEOUT 10
#define MQTTSN_GW_CONN_TIMEOUT 10

// Max buffer size
#define BUFF_SIZE 1000

// Sizing
#define MAX_TOPICS 50
#define MAX_TOPIC_SIZE 50

// Topics vars
int gRegisteredTopics = 0;
char *gTopics[MAX_TOPICS];
int gTopicsIDs[MAX_TOPICS];

// Prepare stream to receive data from Loxone to publish on MQTT-SN Gateway
STREAM *pLoxoneInStream;

// Wait a bit before opening stream, it seems that Loxone sometimes ignore the request if it comes too early
sleep(500);
pLoxoneInStream = stream_create(LISTEN_PORT,0,0);// create udp stream

// PicoC on Loxone does not support 2 dimensions arrays :(
int k;
for (k=0; k<MAX_TOPICS; k++) {
  gTopics[k] = malloc (MAX_TOPIC_SIZE);
}

// MQTT-SN stream, will be created in connect() function
STREAM* pMQTTSNStream;

// Get topic ID, and register it if it does not exist
int getTopicID (char *topic) {

   char szBuffer[BUFF_SIZE], szBufferIn[BUFF_SIZE];
   int nCnt;
   int i;
   int topicID = -1;
   char status[300];
   // Check if topic is registered
   for (i=0; i<gRegisteredTopics; i++) {
      if (strcmp (topic, gTopics[i]) == 0) {
         // Topic already registered
         topicID = gTopicsIDs[i];
         return topicID;
     }
   }

   // Topic not registered, register it
   i = 1;
   // We'll update message length afterwards
   // TODO: handle length > 255
   szBuffer[i++] = 0x0A; // Register
   szBuffer[i++] = 0x00; // TopicID - 0
   szBuffer[i++] = 0x00; // TopicID - 0
   szBuffer[i++] = 0x00; // MsgID - 0
   szBuffer[i++] = 0x01; // MsgID - 1
   strcpy(&szBuffer[i], topic);
   szBuffer[0] = i + strlen(topic);
   // Write to output buffer
   stream_write (pMQTTSNStream, szBuffer, szBuffer[0]); // write to output buffer
   stream_flush (pMQTTSNStream);

   // Wait for answer
   nCnt = stream_read(pMQTTSNStream,szBufferIn,BUFF_SIZE,MQTTSN_GW_TIMEOUT*1000); // read stream, will either reply with 0x18 (DISCONNECT OK) or not reply (no ongoing connection), both are ok

   if ((szBufferIn[1] != 0x0B)|| (szBufferIn[6] != 0x00)) {
      // ERROR
      sprintf (status, "Topic registration Error: %d - %s %d", gRegisteredTopics, topic, szBufferIn[6]);
      setoutputtext (1, status);
   } else {
      // OK, topic registered on Gateway
      strcpy (&gTopics[gRegisteredTopics][0], topic);
      gTopicsIDs[gRegisteredTopics] = (szBufferIn[2] << 8) + szBufferIn[3];
      topicID = gTopicsIDs[gRegisteredTopics];
      gRegisteredTopics++;
   }
   return topicID;
}

// Keepalive function
int keepalive() {

   char szBuffer[3], szBufferIn[BUFF_SIZE];
   int nCnt;
   szBuffer[0] = 0x02; // Length
   szBuffer[1] = 0x16; // Ping

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
   szBuffer[i++] = 0x04; // MsgType: Connect
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
   nCnt = stream_read(pMQTTSNStream,szBufferIn,BUFF_SIZE,MQTTSN_GW_TIMEOUT*1000); // read stream
   if (nCnt == 0)
     return -1;
   return 1;
}

// Disconnect function
int disconnect() {

   char szBuffer[3], szBufferIn[BUFF_SIZE];
   int nCnt;

   szBuffer[0] = 0x02; // Length
   szBuffer[1] = 0x18; // Disconnect

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

// Process publish request received from Loxone
int processPublishRequest(int nCnt, char *message) {

   int i;
   char* pos;
   char topic[200];
   char payload[200];
   char szBuffer[BUFF_SIZE];
   int topicID;
   char status[200];
      
   // Get topic and payload
   i = 0;
   while (1) {
      pos = strstr (&message[i], "/");
      if (pos == NULL)
        break;
      i = strlen (message) - strlen (pos) + 1;
   }
   strncpy (topic, &message[0], i-1);
   strcpy (payload, &message[i]);
        
   topicID = getTopicID (topic);

   // Prepare message
   i = 1;
   // We'll update message length afterwards
   // TODO: handle length > 255
   szBuffer[i++] = 0x0C; // Publish
   szBuffer[i++] = 0x00; // Flag
   szBuffer[i++] = (topicID >> 8); // Topic ID
   szBuffer[i++] = (topicID % 256); // Topic ID
   szBuffer[i++] = 0x00; // MsgID
   szBuffer[i++] = 0x01; // MsgID
   strcpy(&szBuffer[i], payload);
   szBuffer[0] = i + strlen(payload);
        
   stream_write (pMQTTSNStream, szBuffer, szBuffer[0]); // write to output buffer
   stream_flush (pMQTTSNStream);

   sprintf (status, "Published msg: %s : %s", topic, payload);
   setoutputtext (1, status);

   return 1;
}


// Main

// Flush pending connection, if any
disconnect();
int nCnt;
char szBufferIn[BUFF_SIZE];

while (1) {

   while (1) {
      setoutputtext (0, "CONNECTING");
      if (connect() == -1) {
         setoutputtext(0,"Connection failed");
         sleeps (MQTTSN_GW_CONN_TIMEOUT);
      }
      else {
         setoutputtext(0,"CONNECTED");
         gRegisteredTopics = 0;
         sleep (300);
         break;
      }
   }

   while (1) {

      // Process publish requests   
      nCnt = stream_read(pLoxoneInStream,szBufferIn,BUFF_SIZE,MQTTSN_GW_MSG_TIMEOUT*1000);
      if (nCnt > 0) {
         szBufferIn[nCnt] = 0;
         processPublishRequest(nCnt, szBufferIn);
         continue;
      }

      setoutputtext (2, "KEEPALIVE");
      if (keepalive() == 1) {
         // Keep alive ok
         sleep (100);
      }
      else {
         // Connection dead, reconnect
         setoutputtext(0,"CONNECTION DEAD");
         sleep (1000);
         break;
      } 
  }
}
