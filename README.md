# NRF52_Zephyr_Coms_Drivers
Implementing standard coms (i2c, uart and spi) using Zephyr on an nrf52

Here, we will be taking a closer look of how Zephyr is using its own macros to set up and run the three basic com peripherals, that is i2c, uart and SPI.

## General description
We will do our first full peripheral calibration from scratch.

At the end of the repo, we will also discuss Zephyr RTOS a bit but not implement it.

### Driver callbacks
Some driver macros will be using callbacks/interrupts to manage the data flow. This is probably because even when not specifically asked to, Zephyr will implement some RTOS elements in its kernel, meaning that ISRs will have to be used to force the drivers to provide output. (In other words, polling a driver for output as it could be done using the Arduino or the ST environments may not be easily possible here without meddling with Zephyr.)

Callbacks are all event based, and events are text based, meaning that in case of an event, a certain text section within the driver handle will change. An example would be the UART driver, which will have the associated uart struct’s “evt->type” section updated according to whichever event has occurred, for example, uart_rx is finished. This is very much different to bit hunting and may take some time to get used to.

Anwyay, the investigation of the events will then occur within the callback function for the driver.

Mind, the callback function must be externally - static – defined and set up as the callback function for the driver using the appropriate macro. The callback function is always a certain type of function with pre-defined inputs and outputs and thus can’t be randomly generated.

Re-activating a driver after an event is necessary since it is not done automatically. This should be done within the callback function as well.

### Pin that down
If one follows my work here on the ST devices, it should not come as a surprise that defining GPIOs is one of the most important elements of any ST project. The pins have to be “manually” muxed into doing what we want them to (MODER register, AF register). Also, certain peripherals can provide outputs only to certain pins so one has to be very careful at designing the hardware around an ST soc to avoid accidentally locking-out a critical capability by assigning it on the pcb to the wrong pin.

The nrf52 seems to be a lot more agnostic to pin distribution than ST devices, meaning that as long as certain pins are flagged as “General Purpose I/O”, they will be able to provide all output for any fo the peripheral drivers.

This is where the “pinctrl.dtsi” devtree file comes in: that is where we will need to specifically assign pins to the peripherals!

Of course, as usual, things are not straight forwards whatsoever…

When setting up the pin control for the driver, we have to tell the function of the pin. The function of the pin will be defined using the “NRF_PSEL()” macro in the pinctrl file, followed by the selected pin function, the pin group and the pin number. (For the list of pin functions, check in “nrf-pinctrl.h”.)

Another element here is to define the pinctrl for different states of the device. The states are going to most likely be a standard or default mode and some form of sleep or low power mode. Mind, most peripherals will be able to run in both states so it is necessary to assign both within the pinctrl file. (I am yet to go deep enough in the device to define states myself, so I just carried over the two that was originally done for the DK board.)

### Going up that devtree
Now, what needs to be understood is that every peripheral will be a node on the devtree. Mind, there already are nodes in the dtsi files for each peripheral – called “soc level definitions” – but these are not there to actually use the peripheral. They merely give them some base values which are necessary to make the connection between the node’s values and the devtree. Due to build particularities, if a “required” node value is not defined somewhere, the build will fail on the devtree level.

Question is: if we want to define our own node from scratch in the devtree – which we will be doing here – how the heck are we supposed to know the values we will need to provide in our node definition?

Well, the answer lies in the binding yaml files for the nodes. As we already discussed in the previous repo, node type is selected by Zephyr using the “compatible” value of the node, which actually “binds” a respective binding (yaml) file to the node thus defining all the properties of the node. Except that this isn’t technically true: we will have a cluster of yaml files, not just one! This means that if we want to list all the necessary values we need to define, we have to dig deep into the yaml file includes and mentally mark every and each one in the cluster we find with the “required: yes” property added to it. Those values MUST be given to the node to make it work. (For example, to figure out A-to-Z what goes into the i2c master node, we need the “nordic,nrf-twim.yaml”, “nordic,nrf-twi-common.yaml”, “i2c-controller.yaml” and “base.yaml”. The node definition itself will not reflect on this file hierarchy though.)

Of note, other values are not required but very much important to properly calibrate our peripheral. To make another hands-on example , the parent “nordic,nrf-twim.yaml” file – the one we will need to use for i2c - does not have the frequency setup for i2c, that is in going to be in the “i2c-controller.yaml” file.

Lastly, when defining the node for the sensor, the sensor node’s “@” value – the registry cell – MUST be the same as the actual reg value within the sensor node.

### …and the devtree fights back in a very-very dumb way
I came across yet again the same problem as before where certain pins were simply not working for me on my DK. I already guessed that a good number of pins might actually be blocked by external components on the board and that may be the source of my problem.
The solution was a lot simpler than I thought: I merely had to turn the DK board upside down! There all the pins that are in use by the devboard are marked clearly. Suffice to say, if one wants to use those, the output will not be what is expected, as I have found out myself the hard way…

### Driver names
Coming back for a second to the naming convention of device drivers – and compatibility values – these reflect on the driver we are using. “Nordic,nrf-uart” will be the standard uart driver, while “nordic,nrf-uarte” will be the same driver with easyDMA enabled.

Anyway, just be very cautious about what driver you are using – or in what state – and match it up with the appropriate structs, macros and compatibilities, otherwise we may not get the output we wish for.

## To read
Macro information can be searched here:
https://docs.nordicsemi.com/bundle/zephyr-apis-latest/page/index.html
Pin distro (or lack thereof):
https://docs.nordicsemi.com/bundle/ps_nrf52840/page/pin.html
Pin functions:
https://docs.nordicsemi.com/bundle/zephyr-apis-latest/page/nrf-pinctrl_8h.html
Lastly, here is the github of Nordic where some of the exercises I have used as guidelines can be found:
https://github.com/NordicDeveloperAcademy
I recommend this later quite a bit, especially if someone finds my source code and devtree definition hard to follow (like myself sometimes).

## Particularities
These are going to be our first full peripheral calibration studies, so I will be going a bit deeper into the details.

### I2c (blinky_button_isr_timer_log_i2c_custom)
I have found i2c to be the easiest to set up since it does not need any complicated macro magic or callbacks. As such, we will start with that.

#### Felling the tree on i2c
To clarify things, we will be seeing TWIM used instead of I2C in Zephyr a lot here, where TWIM means Two Wire Interface Master. (As far as I can tell, TWI can be used instead of TWIM and will result in the same outcome. TWIS though is a completely different drive where the nrf52 will be an i2c SLAVE.) What we will be doing here is rolling out our trusty BMP280 and hook it up to a custom define I2C output on the board. This will mean a modified custom board and setting up the TWIM peripheral from scratch. For our case here, it will be “TWIM_SCL” and “TWIM_SDA” for SCL and SDA, respectively in the pinctrl file.

Once done, we will set up the sensor itself as a node within the TWIM peripheral node. Mind, sensor drivers can sometimes be available within the Zephyr library so we could potentially just use those to communicate with the sensor – that is, to define the sensor node’s compatibility on the devtree as the actual sensor, not just a simple i2c device.

Here, we will define the BMP280 as a simple i2c device and go from there since it does not have an existing driver in Zephyr (though the “bosch,bme280” might actually work. If memory serves, the difference between the bmp and the bme is simply the slave address.) Of note, if we are using an existing devboard where the sensor is already on the bus, the node will exist in the i2c parent node already so we just need to call the node.

We should not forget to enable the i2c driver in “proj.conf”.

#### Get sourced i2c!
Once everything is sorted, we will go through the usual loops to set the everything up in the source code using the macros: we get the handles, fill up the structs and generally prepare to start sending and receiving messages on the bus.

And then, we ensure that we are NOT overrunning the macro…

…ohh and feed the damn macro the elements it needs to run!

One last thing: if the TWIM driver in Zephyr does not get an ACK from the slave, the macro will throw an error, i.e. return a value that is not 0.

#### Summary
Code section: we have added i2c communications to our code from before. We will simply read out the chip ID from an attached BMP 280 to prove that we have communications with the device.

Board type: custom board v4

### External Uart (blinky_button_isr_timer_log_i2c_uart_custom)
Here we will revisit the uart0 setup we borrowed from the original DK board devtree and replicate it to not just send data to the console, but also send uart serial messages to an additional receiver, say, another mcu connected to the device.

Please note that you will need an external uart device connected to the PC through the terminal to make use of this project section. I used just an Arduino example code to set a Feather board up as the second element in the communication setup. Of note, the nrf52 pins are 5V tolerant, but it is recommended to use a 3v3 I/O logic board as the second device, just to be sure not to damage the nrf52.

#### Felling the tree on uart
Okay, so let’s check the uart devtree definition. We have carried over the uart0 untouched from the original DK devtree and we will still not touch it since it is wired directly to the terminal through the “chosen” in the devtree.

Instead, we will set up serial communications using uart1. To write up the basic node definition, we ought to take a deep dive into the yaml cluster for the “nordic,nrf-uarte” compatibility and find all the required values we have to define: 
-	“reg” and “interrupts” will be defined on the soc level, so we don’t need to redefine those in the dts file. 
-	“pinctrl_0” and “pinctrl_names” should be defined and follow the same naming convention as uart0
-	“current-speed” will be the baud rate, which should match the baud rate on the external bus we intend to interface with (no master-slave setup here, so the bus must be aligned “by hand”)
-	“status” should be “okay” to enable the driver (in Kconfig, it already should be enabled, thanks to the printk project in the previous repo)
-	
With the required “pinctrl” set, we will need to do assign the pin distro, so within the pinctrl.dtsi file we have, we will have to add uart1.

With these set, we will be able to communicate on the bus already at the requested baud rate – here it will be 9600 bdp.

There are some non-required values as well though which are important to set in order to match coms on the bus: 
- “stop-bits” sets the number of stop bits in the messages. Usual value is 1.
- “data-bits” sets the number of bits in a message. Usual value is 8 (or one byte)
- “parity” will add a parity bit the message. Usually not used for simple coms

Lastly, we have some additional hardware configuration options, such as disabling Rx, activating flow control or adding a frame timeout.

Lastly-lastly, the async uart serial API must be enabled in “proj.conf”.

Mind, we can set all these things up within code as well, configuring the uart using the “uart_configure()” function with the uart handle and a “uart_config” struct (that should be defined as a static struct). 

#### Get sourced, uart!
With the devtree sorted, we can start the macro magic...
First thing to note is that unlike i2c which with its master mode could run without interrupts and callbacks, we will need to use ISRs and callbacks to handle the asynch nature of uart. This will make the source code more complicated when communicating compared to i2c.

The UART driver will have an additional struct – the uart event struct - associated with the uart handler, meaning that the events WILL NOT be stored in the device struct! The event struct’s “evt->type” section will be updated according to whichever event has occurred, activating the callback function. The callback function then will need to deal with all events and provide a carry on for the code.

This is going to be obvious to anyone used to object-oriented programming (I am not really one myself), but to access the data section of the event struct, we will need to tap into the data element of the struct and call the required element (example: “evt->data.rx.len” will store the message length of the incoming Rx whever “evt” is “struct uart_event *evt”).

From the event, all types should be self-explanatory, except maybe the “BUF” ones. These are there to allow chain receiving of data. For this to work, we will need to define multiple buffers though. When a buffer is full, these events will trigger and help us juggle the buffers’ contents. We won’t be looking into this option this time.

Of note, we will add a timeout on the rx in order to “guesstimate”, where the incoming uart frame must have ended. This is set in the uart’s enable macro and can be any time frame, infinity included using “SYS_FOREVER_US”.

IMPORTANT: there is a known bug in the handling of the Rx buffer where the timeout detection just doesn’t seem to work well. The solution has been to increase the timeout way above reasonable levels, but without putting it to infinity (see here: Bug in nRF UARTE driver, async Rx buffer handling - Nordic Q&A - Nordic DevZone - Nordic DevZone). If not set, RX_RDY event will be activated haphazardly. I gave “10000” as timeout. Mind, leaving it at infinity will mean that we are not using the timeout in the rx, i.e. RX_RDY will only trigger if the buffer is full. This option should only be used if we know prior to the message is received, how long it is supposed to be (likely not the case).

#### Summary
Code section: we have added an external uart communications to our code from before. We will be waiting for an incoming message on the bus, print out the buffer on the DK’s terminal and then send an acknowledge back on the bus to the source.

Board type: custom board v5, (Adafruit Feather M0 as the other board for uart)

### SPI (blinky_button_isr_timer_log_i2c_uart_spi)
Lastly, we will set up SPI on the DK board to wrap up the basic com peripherals.

The complexity of SPI setup comes from the macros which are more “dynamic” in setting up the peripheral compared to i2c or uart. Heck, even getting the node handler, we will have to feed additional information into the macro…
Anyway, we will again use our trusty BMP280 but instead of connecting it up using i2c, we will rely on SPI.

#### Felling the tree on SPI
We will be using spi1 for this exercise. Why? Because there is a clash between spi0 and uart1, meaning that we can’t run both at the same time.

Setting up the tree itself should be rather self-explanatory after i2c and uart: we find the required values, check the soc for the ones that are already set on the chip and define the rest. Here the only funky thing is with the “cs-gpios” value, which will be an array of CS pins on the bus node itself.  The device node “reg” value then attach the devices to these CS pins in an “enum”-esque fashion (see “spi-controller.yaml”).

Another…ehemm…interesting thing I noticed was how the spi device node compatibility is called. As you have probably noticed, all node compatibility values have a producer element (example: nordic), followed by the actual function. There are exceptions for general drivers though where we have only the peripheral’s name as the compatibility (see i2c). For some reason, spi needs to be compatible with “vnd,spi-device”, despite the fact that the yaml is actually called “spi_device”. I have no exact idea, what “vnd” means, but without adding it, the build will fail. Mind, the “vnd,spi-device” is what is used in all example codes as well. My guess is that the flag for the spi device used to be called as such where “vnd” was just a placeholder for general vendor, but during the many iterations of Zephyr, this flag was not updated. As such, Zephyr will still look for it in the devtree, despite the fact that the flag directs it to the “spi-device” yaml already. 

#### Get sourced, SPI!
First and foremost, we need to remove the i2c check on the bus or at least allow the code to execute in case the bmp280 is not on the bus. This can be achieved easily buy commenting out the blocking return line if the device is not found on the bus. Now, i2c will just throw an error if the bmp280 is not connected but otherwise the code will carry on execution.

Now that we can remove the bmp280 from the i2c bus, we can plug it into spi and do the same thing as we did with i2c, that is, read out the sensor ID. Here it is recommended to define designated read and write functions, otherwise it will become confusing the manage the buffers. Mind, read/transceiver and the write macros both demand “spi_buf_set” structs as buffers, which are just going to be a pointer to the (2D) arrays with the length and the element count of the array added. While it would be sensible to just have one struct of 3 elements holding these three values, the construction of the “spi_buf_set” is instead a struct holding the pointer and the length called “spi_buf”, followed by the number of elements (“buffers”) in the array… Also, just a friendly advice, since the “len” element in “spi_buf”will be the length of the buffer in bytes, it is probably best to just define the data pointer in “spi_buf” as uint8_t* to avoid any confusion. Lastly, since we will have the dummy readout, “len” can never be less than 2.

Regarding the SPI Tx/Rx functions I share here, they are specifically for the BMP280 (or similar sensor), meaning that the MSB for readout is forced to be 1, and 0 for writing. Register values are thus going to be modified within the function to always cater to this change.

#### Summary
Code section: we have added an external spi communications to our code from before. We can pluck the bmp280 either into the i2c bus or the spi bus, both will read out the sensor ID.

Board type: custom board v6, (Adafruit Feather M0 as the other board for uart)

### Board versions
The new board versions are the following:
-	v4: added i2c
-	v5: added external uart1
-	v6: added SPI
We also have an external 3v3 logic Adafruit Feather board as uart source.

### Few words on Zephyr RTOS
Originally, I was planning to do an RTOS section following this repo but, truth be told, the difference between the Zephyr RTOS and FreeRTOS does not really demand an entire repo or even a project. For that, please consult my FreeRTOS implementation repo for STM32.

What should be remembered is that, unlike FreeRTOS which is a tick-based RTOS, Zephyr RTOS is event based and tickless, meaning that its scheduler will only activate when an event is called. These events can be forced to occur at a certain time interval (“time slicing”), effectively turning it into a tick based RTOS, or we can just let the threads themselves call the scheduler by reaching/releasing a semaphore, a mutex or going to sleep…i.e. any time when the state of a thread is changed during execution.

Regarding execution, normally threads of equal priority will execute in a round robin manner, i.e. whichever was the first to be “ready” will go first, just like in FreeRTOS. This can be manipulated by multiple tricks though, such as using time slicing to enforce parallel execution, or by defining deadlines of execution for the threads where the thread with a shorter deadline will pre-empt the other thread with the same priority.

Threads can be “blocked” from being interrupted if they are defined as such (called “cooperative threads”), meaning that a critical section could be added to the entirety of the thread by definition (unlike in FreeRTOS where the thread had to manually block the scheduler). This is done by simply having negative priority by definition on a thread.

Zephyr RTOS also has something called a “workqueue system” where non-critical action can be dumped. It is a flexible thread with low priority relative to threads and behaves like a FIFO of functions/actions/”works”. Mind, these “works” do need to be called with the appropriate syntax, i.e. generate a struct for them, a callback function and call the right macros to start the work queue (“k_work_queue_start”), define the work (“k_work_init”) and add it to the work queue (“k_work_submit_to_queue”). Work queues are used extensively when running Bluetooth to avoid the timing-based collapse of the stack.

Queues work a bit different compared to FreeRTOS. Here you can define the messages – the element size of the queue – yourself.

Zephyr has FIFOs to pass any undefined data sizes, though in this particular case, the FIFO will only store the pointer to the data element and pass the pointer around instead of the data package itself. This means that all data packages must be allocated with malloc and released later when a FIFO is not used anymore. The allocation can be dynamic or static, it doesn’t matter, but must be managed in code.

To conclude, the comments I have expressed for FreeRTOS in my “STM32_RTOS” repo still stand: RTOS is a powerful tool but not necessary for all applications. Here in Zephyr we are a bit forced to cohabitate with an RTOS which brings problems of itself, for instance, how often something non-blocking (non-IST) function like printk might be “ignored” due to pre-emption or timing. Remember that whenever you are doing a simple superloop and something strange happens.

## User guide
A short description on what is going on in each project elements was provided above.

## Conclusion
Here we have implemented the three standard com peripherals using Zephyr. Making good use of other simple peripherals such as PWM or ADC or RTC or QSPI should be rather straight forwards and would demand of me repeating what I have presented here so I won’t make a repo on them. At any case, anyone getting a grip of Zephyr enough to arrive here should have no issue making these other peripherals work.
Next, we will implement a BLE server on the nrf52.
