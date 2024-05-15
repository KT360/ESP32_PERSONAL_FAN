import React, {useState} from 'react';
import {
  TouchableOpacity,
  Button,
  PermissionsAndroid,
  View,
  Text,
} from 'react-native';

import base64 from 'react-native-base64';


import {BleManager, Device} from 'react-native-ble-plx';
import {styles} from './Styles/styles';
import {LogBox} from 'react-native';
import VerticalSlider from 'react-native-vertical-slider-smartlife';

LogBox.ignoreLogs(['new NativeEventEmitter']); // Ignore log notification by message
LogBox.ignoreAllLogs(); //Ignore all log notifications

const BLTManager = new BleManager();

const SERVICE_UUID = '';

const MESSAGE_UUID = '';
const RPM_UUID = '';

//TODO: COMMENTS

export default function App() {
  //Is a device connected?
  const [isConnected, setIsConnected] = useState(false);

  //What device is connected?
  const [connectedDevice, setConnectedDevice] = useState<Device>();

  //Data for sending and recieving
  const [message, setMessage] = useState('Nothing Yet');
  const [rpm, setRPM] = useState(0);

  //To check if permissions have already been granted
  const [is_permitted, setIsPermitted] = useState(false);

  async function requestLocationPermission() {

    try
    {
      const granted = await PermissionsAndroid.request(
        PermissionsAndroid.PERMISSIONS.ACCESS_FINE_LOCATION,
        {
          title: 'Permission Localisation Bluetooth',
          message: 'Requirement for Bluetooth',
          buttonNeutral: 'Later',
          buttonNegative: 'Cancel',
          buttonPositive: 'OK',
        },
      );
  
      if(granted === PermissionsAndroid.RESULTS.GRANTED)
        {
          console.log("Permissions Granted");
          return true;
        }else
        {
          console.log("Permissions Revoked");
          return false;
        }
    }catch(err)
    {
      console.warn(err);
      return false;
    }
  
  } 
  // Scans availbale BLT Devices and then call connectDevice
  //Check for permissions and already existing devices
  async function scanDevices() {

    if(is_permitted == false)
    {
      const permission = await requestLocationPermission();
      setIsPermitted(permission);
    }

    if(is_permitted)
    {
      if(connectedDevice)
      {
        connectDevice(connectedDevice!);
      }else
      {
        console.log('scanning');

        BLTManager.startDeviceScan(null, null, (error, scannedDevice) => {
          if (error) {
            console.warn(error);
          }
  
          if (scannedDevice && scannedDevice.name == 'ESPFAN') {
            BLTManager.stopDeviceScan();
            connectDevice(scannedDevice);
          }
        });
  
        // stop scanning devices after 5 seconds
        setTimeout(() => {
          BLTManager.stopDeviceScan();
        }, 5000);
      }
    }

  }

  // handle the device disconnection
  async function disconnectDevice() {

    console.log('Disconnecting start');

    if (connectedDevice != null) {

      const isDeviceConnected = await connectedDevice.isConnected();

      if (isDeviceConnected) {

        BLTManager.cancelTransaction('messagetransaction');
        BLTManager.cancelTransaction('nightmodetransaction');

        try
        {
          // Wait for the disconnection to complete before checking the connection status
          BLTManager.cancelDeviceConnection(connectedDevice.id);

          const connectionStatus = await connectedDevice.isConnected();
          if (!connectionStatus) {

            setIsConnected(false);
            console.log('Disconnection completed');

          }else
          {
            console.log('Device still connected');
          }
        } catch(error)
        {
          console.log('Error during disconnection: ', error);
        }

      }else
      {
        console.log('Device is already disconnected');
      }
    }
  }

  //Function to send data to ESP32
  async function sendRPMValue(value: number) {
    if(connectedDevice)
    {
      BLTManager.writeCharacteristicWithResponseForDevice(
        connectedDevice.id,
        SERVICE_UUID,
        RPM_UUID,
        base64.encode(value.toString()),
      ).then(characteristic => {

        console.log('RPM changed to :', base64.decode(characteristic.value || "No value"));

      });
    }
  }
  //Connect the device and start monitoring characteristics
  async function connectDevice(device: Device) {

    console.log('connecting to Device:', device.name);

    try
    {
      const conn_device = await device.connect();

      setConnectedDevice(conn_device);
      setIsConnected(true);

      await conn_device.discoverAllServicesAndCharacteristics();

      BLTManager.onDeviceDisconnected(device.id, async(error, device) => {
        
        console.log('Device DC');
        setIsConnected(false);
        setConnectedDevice(undefined);

      });

      //Read inital values

      //Message
      device.readCharacteristicForService(SERVICE_UUID, MESSAGE_UUID)
        .then(valenc => {
          setMessage(base64.decode(valenc.value || "No value").replace("Â",""));
        });

        //RPM
        device.readCharacteristicForService(SERVICE_UUID, RPM_UUID)
          .then(valenc => {
            
            let rpm_value = parseInt(base64.decode(valenc.value || "No value"));

            if(rpm_value)
            {
              setRPM(rpm_value);
            }

          });

      //monitor values and tell what to do when receiving an update
      //Sometimes Temp value comes with some string errors, ex:.replace("Â",""), so remove them

      //Message
      device.monitorCharacteristicForService(
        SERVICE_UUID,
        MESSAGE_UUID,

        (error, characteristic) => {

          if (characteristic?.value != null) {
            
            setMessage(base64.decode(characteristic?.value).replace("Â",""));
            
            console.log(
              'Message update received: ',
              base64.decode(characteristic?.value),
            );
          }
        },
        'messagetransaction',
      );

      console.log('Connection established');

    }catch(conn_error)
    {
      console.error('Failed to connect: ', conn_error);
      setIsConnected(false);
    }
  }

  return (
    <View>
      <View style={{paddingBottom: 100}}></View>

      {/* Title */}
      <View style={styles.rowView}>
        <Text style={styles.titleText}>FAN PROJECT</Text>
      </View>

      <View style={{paddingBottom: 20}}></View>

      {/* Monitored Value */}

      <View style={styles.rowView}>
        <Text style={styles.baseText}>{message}</Text>
      </View>

      <View style={{paddingBottom: 20}}></View>

      {isConnected ? 
        <View style={styles.rowView}>
          <VerticalSlider
          value={rpm}
          disabled={false}
          min={0}
          max={1500}
          onComplete={(value: number) => {
            sendRPMValue(value);
            setRPM(value);
            console.log("COMPLETE", value);
          }}
          width={50}
          height={300}
          step={50}
          borderRadius={5}
          minimumTrackTintColor={"gray"}
          maximumTrackTintColor={"tomato"}
          showBallIndicator
          ballIndicatorColor={"gray"}
          ballIndicatorTextColor={"white"}
          />
        </View>
      
      :
        null
      }

      <View style={{paddingBottom: 20}}></View>

      {/* Connect Button */}
      <View style={styles.rowView}>
        <TouchableOpacity style={{width: 120}}>
          {!isConnected ? (
            <Button
              title="Connect"
              onPress={() => {
                scanDevices();
              }}
              disabled={false}
            />
          ) : (
            <Button
              title="Disonnect"
              onPress={() => {
                disconnectDevice();
              }}
              disabled={false}
            />
          )}
        </TouchableOpacity>
      </View>

      <View style={{paddingBottom: 20}}></View>
    </View>
  );
}
