/*
 * liquid lifebar by bfayer@gmail.com
 * 
 * Hardware:
 * Motor controler: pololu jrk21v3, linear actualtor LACT2P
 * Arduino UNO R3
 *
 * Motor controller config: Pololu jrk config utility in Serial mode using UART detect baud rate interface. 
 * starting with the default configuration settings for LACT2P linear actuators provided on the pololu website
 *
 * pin 7 connected to jrk pin Tx
 * pin 8 connected to jrk pin Rx
 * pin 5 = red LED (pwm)
 * pin 6 = green LED (pwm)
 * pin 9 = blue LED (pwm)
 * pin 11 = Ultra Violet LED (pwm)
 * pin 3 = air pump  (pwm)
 */


#include <SoftwareSerial.h>

SoftwareSerial mySerial(7,8); // RX, TX, plug your control line into pin 8 and connect it to the RX pin on the JRK21v3

int debug;

// Setting pwm pins
int redPin = 5;
int grnPin = 6;
int bluPin = 9;
int uvPin = 11; 
int pumpPin = 3;

//the health level at any point in time
int hp; 
int preHP; //was used when trying to figure out if there was a change in hp since last transmition 

//stuff used for input from pc
byte buffer[8] ;
int pointer = 0; 
byte inByte = 0;  

//Variables used for the forced fading process
int redVal = 0;
int grnVal = 255;
int bluVal = 0;
int uvVal = 255; //setting uv brightness 

//Targets from PC
int redTarget=0;
int grnTarget=255;
int bluTarget=0;
int uvTarget=0;
int pumpSet=0;

//Previous targets for fading purposes
int prevR = 0;
int prevG = 0;
int prevB = 0;
int prevUV = 0;
int dimDelay = 3;

//Motor position variables for limiting range and position relative fading
int minMotorPosition = 2500; //manually calibrated, plan on setting up low end calibration through pc app
int maxMotorPosition = 3600; //manually calibrated
int motorTarget = maxMotorPosition;
int preMotorTarget = minMotorPosition;
int state = 1; 

// HP announcer for Serial COM
void announceHP(int (health)) {
  Serial.print("Health level set to ");
  Serial.print(health);
  Serial.println("%");
} 


//figures out a modulus factor to fade all colors in exactly 510 steps
int calculateStep(int prevValue, int endValue) {
  int step = endValue - prevValue; // What's the overall gap?
  if (step) {                      // If its non-zero, 
    step = 510/step;    //   divide by 510
  } 
  return step;
}

//function used by transitionToNew to increment colors besed on their step modulus
int calculateVal(int step, int val, int i) {

  if ((step) && i % step == 0) { // If step is non-zero and its time to change a value,
    if (step > 0) {              //   increment the value if step is positive...
      val += 1;           
    } 
    else if (step < 0) {         //   ...or decrement it if step is negative
      val -= 1;
    } 
  }

  // keeps values within range
  if (val > 255) {
    val = 255;
  } 
  else if (val < 0) {
    val = 0;
  }
  return val;
}


//sets the new target for the JRK21V3 controller, this uses pololu high resulution protocal
void Move(int x) {
  preMotorTarget = jrkGetFeedback(); //motorTarget;
  float motorScale = (maxMotorPosition-minMotorPosition)/100;
  motorTarget=(x*motorScale)+minMotorPosition;
  word target = motorTarget;  //only pass this ints, i tried doing math in this and the remainder error screwed something up
  mySerial.write(0xAA); //tells the controller we're starting to send it commands
  mySerial.write(0xB);   //This is the pololu device # you're connected too that is found in the config utility(converted to hex). I'm using #11 in this example
  mySerial.write(0x40 + (target & 0x1F)); //first half of the target, see the pololu jrk manual for more specifics
  mySerial.write((target >> 5) & 0x7F);   //second half of the target, " " " 
}  


// Set up outputs, and start at assumed 100% hp
void setup()
{
  mySerial.begin(9600);
  Serial.begin(19200);
  Serial.println("#Health meter bar initialized");
  hp = 100;
  Move(hp);
  preHP=hp; 
  fullLife(); 
  announceHP(int(hp));

  Serial.flush();// Give reader a chance to see the output.


  //debug is 1 for on 0 for off
  debug = 0;

  //setup PWM output pins
  pinMode(redPin, OUTPUT);
  pinMode(grnPin, OUTPUT);
  pinMode(bluPin, OUTPUT);
  pinMode(uvPin, OUTPUT);
  pinMode(pumpPin, OUTPUT);

  analogWrite(pumpPin,40);
}



// Force fading between previous color and new color
void transitionToNew(int R,int G, int B, int UV, int thisDelay){

  int stepR = calculateStep(prevR, R);
  int stepG = calculateStep(prevG, G); 
  int stepB = calculateStep(prevB, B);
  int stepUV = calculateStep(prevUV, UV);
  preHP = hp;


  for (int i = 0; i <= 510; i++) {

    redVal = calculateVal(stepR, redVal, i);
    grnVal = calculateVal(stepG, grnVal, i);
    bluVal = calculateVal(stepB, bluVal, i);
    uvVal = calculateVal(stepUV, uvVal, i);
    if (Serial.available() >0)
    {
      break;
    }

    analogWrite(redPin, redVal);   // Write current values to LED pins
    analogWrite(grnPin, grnVal);      
    analogWrite(bluPin, bluVal); 
    analogWrite(uvPin, uvVal);
    delay(thisDelay); // Pause for 'wait' milliseconds before resuming the loop

  }
  // Update current values for next loop
  prevR = redVal; 
  prevG = grnVal; 
  prevB = bluVal;
  prevUV = uvVal;

}

//special case for when hp is set to 0
void death() { 
  pumpLevel(0);
  Serial.println("Death");
  transitionToNew(255,0,0,0,2);
  transitionToNew(0,0,0,0,6);
  state=0;
}


//special case that triggers on a full heal from another state
void fullLife() {
  transitionToNew(255,255,255,0,2);//fades to white
  transitionToNew(redTarget,grnTarget,bluTarget,uvTarget,1);//force fades back to target
}


//writes an int to the pump pin
void pumpLevel(int pumpIncoming) {   
  analogWrite(pumpPin,pumpIncoming);
}

//Updates RGB LED and UV LED states
void updateState(){
  int feedback = jrkGetFeedback();
  if(abs(feedback-motorTarget)<30) { //if the fade is near completion stop attempting positional fade (it never gets there perfectly)
    state=1; //switch to forced fade state.
  }
  else{
  double posFrac = (double(feedback)-double(preMotorTarget))/(double(motorTarget)-double(preMotorTarget));//this is the fractional position of travel between the previous target  and the new target.
  redVal = calculateVal2(prevR,redTarget,posFrac);
  grnVal = calculateVal2(prevG,grnTarget,posFrac);
  bluVal = calculateVal2(prevB,bluTarget,posFrac);
  uvVal = calculateVal2(prevUV,uvTarget,posFrac);
  analogWrite(redPin, redVal);   // Write current values to LED pins
  analogWrite(grnPin, grnVal);      
  analogWrite(bluPin, bluVal); 
  analogWrite(uvPin, uvVal);
  }

}

  
  
//send this a pre and target int and it spits out an int based on the fractional feedback position of the motor controller
int calculateVal2(int previousSetting, int targetSetting, double positionFraction){
  //delay(10); // for some reason this delay seemed to solve a wierd flickering bug but the bug disappeared.. 
  int returnVal;
  if (previousSetting==targetSetting){ //doesn't attempt to fade anything if it's already the right color
    returnVal= targetSetting;
  }
  else {    //some math to calculate the next int step relative to the motor position
    
    returnVal = (previousSetting)+(targetSetting-previousSetting)*positionFraction;

    if (returnVal > 255) {
      returnVal = 255;
    } 
    else if (returnVal < 0) {
      returnVal = 0;
    }
  }
    return returnVal;
}

//gets the current position from the jrk motor controller
int jrkGetFeedback() {
  mySerial.write(0xAA); //tells the controller we're starting to send it commands
  mySerial.write(0xB); //which motor controller im talking to
  mySerial.write(0xA5); //says  HEY! send me a feedback output
  unsigned char response[2];
  delay(5); //needs to wait for the response, testing showed this to be the stable point

  if (mySerial.available()){
    response[0]=mySerial.read();
    response[1]=mySerial.read();
  }
  return response[0] + 256*response[1]; 
}



void loop()
{

  // check serial port for input from pc
  if (Serial.available() >0) {
    // read the incoming byte:
    inByte = Serial.read();
    delay(10);
    // The "#" sign incoming starts filling the serial read buffer array, otherwise nothing happens
    if (inByte == '#') {
      while (pointer < 7) { // accumulate chars in the buffer
        buffer[pointer] = Serial.read(); 
        pointer++;
        //delay(1);
      }
      Serial.flush();

      //logs the current settings for reference (for fading mainly)
      preHP=hp;
      prevR=redVal;
      prevG=grnVal;
      prevB=bluVal; 
      prevUV=uvVal;
      
      //updates all of the taret variables.
      hp=buffer[0];
      redTarget=buffer[1];
      grnTarget=buffer[2];
      bluTarget=buffer[3];
      uvTarget=buffer[4];
      pumpSet=buffer[5];
      debug=buffer[6];

      //updated the pump speed (response is so slow that there is no reason to fade it)
      pumpLevel(pumpSet);

      //debug stuff
      if (debug !=0){   // reports the serial port input values back to pc
        Serial.println("Debug mode on");
        Serial.print("HP:");
        Serial.println(hp);
        Serial.print("R:");
        Serial.print(redTarget);
        Serial.print(", G:");
        Serial.print(grnTarget); 
        Serial.print(", B:");
        Serial.print(bluTarget);
        Serial.print(", UV:");
        Serial.println(uvTarget);
        Serial.print("PumpSet:");
        Serial.println(pumpSet);
        Serial.print("debug:");
        Serial.println(debug);
      }   


      /*main decision tree for what to do with new position or color data
       * State 0 = Color fading relative to motor position
       * State 1 = Forced color transitioning  
       * tbd- state 2 will be segmented color without fade maybe.
       */
      if (preHP==hp) {
        state=1;
        transitionToNew(redTarget,grnTarget,bluTarget,uvTarget,dimDelay);
      }
      else if (hp <= 0){  //handling special case for death 
        state=1; 
        hp = 0;
        Move(hp);
        death();
      }
      else if (hp >= 100){  //handling special case for heals to full hp
        state=1; 
        hp=100;
        Move(hp);
        fullLife();
      }
      else if (abs(hp-preHP) <=3){
        state = 1;
        Move(hp);
        transitionToNew(redTarget,grnTarget,bluTarget,uvTarget,0);
//        analogWrite(redPin, redTarget);   // Write current values to LED pins
//        analogWrite(grnPin, grnTarget);      
//        analogWrite(bluPin, bluTarget); 
//        analogWrite(uvPin, uvTarget);
      }
      else {   // if not dying or full hp than just move and fade to new position
        state = 0; 
        Move(hp);
        dimDelay = 1;
      }

      announceHP(hp); //echos the new hp back to pc
      pointer=0;  //resets the pointer for the serial reader
    } 
  }
  if (state ==0){   //0 state is when machine is fading relative to motor position between colors (no forced fade)
    updateState();
  }

}






