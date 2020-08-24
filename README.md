# Proxom
Simple proxy made for playing Among Us(http://www.innersloth.com/gameAmongUs.php) in p2p mode.
This proxy is based on the "sudppipe" project, which can be found here: https://aluigi.altervista.org/mytoolz.htm. It also uses the network library provided by the CSFML API:https://www.sfml-dev.org/.

```
Usage: ./proxom -s server -m message -b true/false -c combinationKey -r startHotKey -t stopHotKey



Options:
  -s: The server to which to forward the packets
  -m: The message to be broadcasted on port 47777(the port that is used for the game in order to broadcast the local games)
  -b: Disables/Enables the feature that allows the user to disable the game broadcast while Proxom is running. NOTE: this feature uses the WINAPI RegisterHotKey function, so it will make the chosen hotkey unavailable for any other app. This feature is enabled by default and it uses the following combinations of keys: Alt + J (in order to stop the broadcast), Alt + B (in order to restart the broadcast after it has been stopped)
  -c: The modifier key (combination key) which should be pressed in combination with the startHotKey key or the stopHotKey key. The values can be one of the following: shift, ctrl, alt
  -r: The key that should be pressed while the combinationKey is hold down. This key should only be an alphabet key. This hotkey will restart the broadcast of the server if it is stopped
  -t: The key that should be pressed while the combinationKey is hold down. This key should only be an alphabet key. This hotkey will stop the broadcast of the server if it is running
  NOTE: No argument is required to run the Proxom app. If the server argument is not specified, the program will ask for the server ip address at startup. If the "-b" argument is false, the "-c", "-r", "-t" arguments will be ignored.
  
```
