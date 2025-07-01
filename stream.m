host = '10.246.57.218';  % This is our Linux PC Ip >> Later we gonna change to DAQ when deploying on it 
port = 12345;       

% TCP client
client = tcpclient(host, port);

disp('[MATLAB] Connected to server.');

for i = 1:25  % Match LOOP_COUNT in C++ or use 'while true'
    if client.NumBytesAvailable > 0
        data = readline(client);  
        disp(['[MATLAB] Received: ', data]);

        numericData = str2double(strsplit(strtrim(data), ','));
        disp('[MATLAB] Parsed values:');
        disp(numericData);

    else
        pause(0.1);  
    end
end

% Cleanup (optional)
clear client;
