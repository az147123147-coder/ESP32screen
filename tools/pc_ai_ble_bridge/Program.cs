using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Windows.Devices.Bluetooth;
using Windows.Devices.Bluetooth.Advertisement;
using Windows.Devices.Bluetooth.GenericAttributeProfile;
using Windows.Devices.Enumeration;
using Windows.Foundation;
using Windows.Storage.Streams;

namespace PcAiBleBridge
{
    internal static class Program
    {
        private static readonly Guid ServiceUuid = new Guid("7f510001-1b15-4c9b-9f2d-6d2e5a7c1000");
        private static readonly Guid StatusCharacteristicUuid = new Guid("7f510002-1b15-4c9b-9f2d-6d2e5a7c1000");
        private static readonly Guid DeviceInfoCharacteristicUuid = new Guid("7f510003-1b15-4c9b-9f2d-6d2e5a7c1000");
        private static readonly TimeSpan ScanTimeout = TimeSpan.FromSeconds(12);
        private static readonly TimeSpan ConnectTimeout = TimeSpan.FromSeconds(10);
        private static readonly TimeSpan DiscoveryTimeout = TimeSpan.FromSeconds(10);
        private static readonly TimeSpan IoTimeout = TimeSpan.FromSeconds(5);
        private static readonly TimeSpan DetailIoTimeout = TimeSpan.FromSeconds(3);
        private static readonly TimeSpan HeartbeatInterval = TimeSpan.FromSeconds(2);
        private static readonly TimeSpan DetailFailureRetryDelay = TimeSpan.FromMilliseconds(250);
        private static readonly TimeSpan DisconnectGracePeriod = TimeSpan.FromSeconds(10);
        private static readonly TimeSpan StableConnectionTime = TimeSpan.FromSeconds(30);
        private static readonly int[] RetrySeconds = { 1, 2, 4, 8, 15 };
        private static readonly byte[] ExpectedDeviceInfoPrefix = { 0xA1, 0x01, 0x08, 0x03, 0x0F, 0x02, 0x0A };
        private static readonly CancellationTokenSource Shutdown = new CancellationTokenSource();
        private static readonly Random Random = new Random();
        private static byte sequence = (byte)Random.Next(0, 256);

        private const byte StateOff = 0;
        private const byte StateIdle = 1;
        private const byte StateBusy = 2;
        private const byte StateUnknown = 3;
        private const string DeviceName = "ESP32-AI-Status";
        private const string IsPresentProperty = "System.Devices.Aep.IsPresent";
        private const string IsConnectableProperty = "System.Devices.Aep.Bluetooth.Le.IsConnectable";
        private const string PairingResetHint = "; if Windows and the screen have different pairing records, stop the bridge, remove ESP32-AI-Status in Windows, press FORGET on the screen, press PAIR on the screen, and pair again in Windows Settings";
        private static readonly ProcessActivityDetector CodexActivityDetector =
            new ProcessActivityDetector(new string[] { "codex", "codex-code-mode-host" });
        private static readonly ProcessActivityDetector ClaudeActivityDetector =
            new ProcessActivityDetector(new string[] { "claude" }, false);
        private static readonly CodexTranscriptMonitor CodexTranscripts = new CodexTranscriptMonitor();
        private static readonly ClaudeDesktopLogMonitor ClaudeDesktopLogs = new ClaudeDesktopLogMonitor();
        private static readonly ProcessSnapshotMonitor ProcessSnapshots = new ProcessSnapshotMonitor();

        private const byte DetailOff = 0;
        private const byte DetailIdle = 1;
        private const byte DetailBusy = 2;
        private const byte DetailWait = 3;
        private const byte DetailDone = 4;
        private const byte DetailFailed = 5;
        private const byte DetailStale = 6;
        private const byte ProductCodex = 0;

        private static int Main(string[] args)
        {
            if (args.Length != 0)
            {
                Console.Error.WriteLine("This program does not accept command-line arguments.");
                return 2;
            }

            Mutex instanceMutex = null;
            bool ownsInstanceMutex = false;

            try
            {
                instanceMutex = new Mutex(false, "Local\\PcAiBleBridge_7F510001_1B15_4C9B_9F2D_6D2E5A7C1000");

                try
                {
                    ownsInstanceMutex = instanceMutex.WaitOne(0, false);
                }
                catch (AbandonedMutexException)
                {
                    ownsInstanceMutex = true;
                }

                if (!ownsInstanceMutex)
                {
                    Console.Error.WriteLine("Bridge terminated: another bridge instance is already running.");
                    return 3;
                }

                Console.CancelKeyPress += OnCancelKeyPress;
                CodexTranscripts.Start();
                ProcessSnapshots.Start();
                WriteLog("bridge started");
                RunAsync(Shutdown.Token).GetAwaiter().GetResult();
                WriteLog("bridge stopped");
                return 0;
            }
            catch (OperationCanceledException)
            {
                WriteLog("bridge stopped");
                return 0;
            }
            catch (PairingRequiredException ex)
            {
                Console.Error.WriteLine("Bridge terminated: {0}", ex.Message);
                return 1;
            }
            catch (FatalBridgeException ex)
            {
                Console.Error.WriteLine("Bridge terminated: {0}", ex.Message);
                return 1;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine("Bridge terminated: {0}", FormatException(ex));
                return 1;
            }
            finally
            {
                Console.CancelKeyPress -= OnCancelKeyPress;
                ProcessSnapshots.Dispose();
                CodexTranscripts.Dispose();
                Shutdown.Dispose();

                if (ownsInstanceMutex)
                {
                    try
                    {
                        instanceMutex.ReleaseMutex();
                    }
                    catch (ApplicationException)
                    {
                    }
                }

                if (instanceMutex != null)
                {
                    instanceMutex.Dispose();
                }
            }
        }

        private static void OnCancelKeyPress(object sender, ConsoleCancelEventArgs args)
        {
            args.Cancel = true;
            Shutdown.Cancel();
        }

        private static async Task RunAsync(CancellationToken cancellationToken)
        {
            int retryLevel = 0;
            string preferredDeviceId = null;

            using (PairedDeviceCatalog catalog = new PairedDeviceCatalog())
            {
                await catalog.StartAsync(cancellationToken).ConfigureAwait(false);

                while (true)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    BleConnection connection = null;
                    DateTime readyAtUtc = DateTime.MinValue;

                    try
                    {
                        WriteLog("looking for paired compatible screen");
                        ConnectionResult result = await FindAndConnectAsync(catalog, preferredDeviceId, cancellationToken).ConfigureAwait(false);
                        connection = result.Connection;
                        preferredDeviceId = result.DeviceId;
                        readyAtUtc = DateTime.UtcNow;
                        WriteLog("compatible screen connected");
                        await PublishLoopAsync(connection, cancellationToken).ConfigureAwait(false);
                    }
                    catch (OperationCanceledException)
                    {
                        if (cancellationToken.IsCancellationRequested)
                        {
                            cancellationToken.ThrowIfCancellationRequested();
                        }

                        WriteLog("BLE operation timed out");
                    }
                    catch (PairingRequiredException)
                    {
                        throw;
                    }
                    catch (FatalBridgeException)
                    {
                        throw;
                    }
                    catch (BridgeException ex)
                    {
                        WriteLog(ex.Message);
                    }
                    catch (Exception ex)
                    {
                        WriteLog("BLE operation failed: " + FormatException(ex));
                    }
                    finally
                    {
                        if (connection != null)
                        {
                            connection.Dispose();
                        }
                    }

                    if (readyAtUtc != DateTime.MinValue && DateTime.UtcNow - readyAtUtc >= StableConnectionTime)
                    {
                        retryLevel = 0;
                    }

                    int delaySeconds = RetrySeconds[Math.Min(retryLevel, RetrySeconds.Length - 1)];
                    int delayMilliseconds = checked(delaySeconds * 1000 + Random.Next(0, 401));
                    WriteLog("retrying in " + delaySeconds + " seconds");
                    await Task.Delay(delayMilliseconds, cancellationToken).ConfigureAwait(false);

                    if (retryLevel < RetrySeconds.Length - 1)
                    {
                        retryLevel++;
                    }
                }
            }
        }

        private static async Task<ConnectionResult> FindAndConnectAsync(PairedDeviceCatalog catalog,
                                                                         string preferredDeviceId,
                                                                         CancellationToken cancellationToken)
        {
            HashSet<string> attemptedIds = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

            if (!string.IsNullOrEmpty(preferredDeviceId))
            {
                attemptedIds.Add(preferredDeviceId);

                try
                {
                    BleConnection preferredConnection = await ConnectAsync(preferredDeviceId, cancellationToken).ConfigureAwait(false);
                    return new ConnectionResult(preferredDeviceId, preferredConnection);
                }
                catch (FatalBridgeException ex)
                {
                    if (ex.IsGlobal)
                    {
                        throw;
                    }

                    WriteLog("previously connected screen is incompatible: " + ex.Message);
                }
                catch (BridgeException ex)
                {
                    WriteLog("previously connected screen is unavailable: " + ex.Message);
                }
            }

            List<PairedDeviceCandidate> candidates = catalog.GetScreenCandidates(preferredDeviceId);

            for (int index = 0; index < candidates.Count; index++)
            {
                PairedDeviceCandidate candidate = candidates[index];
                if (!attemptedIds.Add(candidate.Id))
                {
                    continue;
                }

                try
                {
                    BleConnection connection = await ConnectAsync(candidate.Id, cancellationToken).ConfigureAwait(false);
                    return new ConnectionResult(candidate.Id, connection);
                }
                catch (FatalBridgeException ex)
                {
                    if (ex.IsGlobal)
                    {
                        throw;
                    }

                    WriteLog("paired screen candidate is incompatible: " + ex.Message);
                }
                catch (BridgeException ex)
                {
                    WriteLog("paired screen candidate is unavailable: " + ex.Message);
                }
            }

            WriteLog("no usable paired screen was found; scanning advertisements as fallback");

            ScanOutcome scanned = await ScanAsync(cancellationToken).ConfigureAwait(false);
            string scannedDeviceId = await ResolveScannedDeviceIdAsync(scanned, cancellationToken).ConfigureAwait(false);

            if (!attemptedIds.Add(scannedDeviceId))
            {
                WriteLog("screen advertisement refreshed; retrying the paired device");
            }

            try
            {
                BleConnection scannedConnection = await ConnectAsync(scannedDeviceId, cancellationToken).ConfigureAwait(false);
                return new ConnectionResult(scannedDeviceId, scannedConnection);
            }
            catch (FatalBridgeException ex)
            {
                if (ex.IsGlobal)
                {
                    throw;
                }

                throw new BridgeException("advertised screen is incompatible: " + ex.Message);
            }
        }

        private static async Task<string> ResolveScannedDeviceIdAsync(ScanOutcome scanned, CancellationToken cancellationToken)
        {
            BluetoothLEDevice device = await RunWithTimeoutAsync(
                delegate(CancellationToken operationToken)
                {
                    if (scanned.AddressType == BluetoothAddressType.Unspecified)
                    {
                        return ToTask<BluetoothLEDevice>(BluetoothLEDevice.FromBluetoothAddressAsync(scanned.Address), operationToken);
                    }

                    return ToTask<BluetoothLEDevice>(BluetoothLEDevice.FromBluetoothAddressAsync(scanned.Address, scanned.AddressType), operationToken);
                },
                ConnectTimeout,
                cancellationToken,
                "advertised device lookup").ConfigureAwait(false);

            if (device == null)
            {
                throw new BridgeException("advertised screen could not be opened");
            }

            try
            {
                if (!device.DeviceInformation.Pairing.IsPaired)
                {
                    throw new PairingRequiredException("screen was discovered but is not paired in Windows; stop the bridge, press PAIR on the screen, pair ESP32-AI-Status in Windows Settings, then start the bridge again");
                }

                return device.DeviceInformation.Id;
            }
            finally
            {
                try
                {
                    device.Dispose();
                }
                catch (Exception)
                {
                }
            }
        }

        private static async Task<ScanOutcome> ScanAsync(CancellationToken cancellationToken)
        {
            BluetoothLEAdvertisementWatcher watcher = new BluetoothLEAdvertisementWatcher();
            TaskCompletionSource<ScanOutcome> completion = new TaskCompletionSource<ScanOutcome>(TaskCreationOptions.RunContinuationsAsynchronously);
            TypedEventHandler<BluetoothLEAdvertisementWatcher, BluetoothLEAdvertisementReceivedEventArgs> receivedHandler = null;
            TypedEventHandler<BluetoothLEAdvertisementWatcher, BluetoothLEAdvertisementWatcherStoppedEventArgs> stoppedHandler = null;

            watcher.ScanningMode = BluetoothLEScanningMode.Active;
            watcher.AdvertisementFilter.Advertisement.ServiceUuids.Add(ServiceUuid);

            receivedHandler = delegate(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementReceivedEventArgs eventArgs)
            {
                completion.TrySetResult(ScanOutcome.Found(eventArgs.BluetoothAddress, eventArgs.BluetoothAddressType));
            };

            stoppedHandler = delegate(BluetoothLEAdvertisementWatcher sender, BluetoothLEAdvertisementWatcherStoppedEventArgs eventArgs)
            {
                completion.TrySetResult(ScanOutcome.Stopped());
            };

            watcher.Received += receivedHandler;
            watcher.Stopped += stoppedHandler;

            try
            {
                try
                {
                    watcher.Start();
                }
                catch (Exception)
                {
                    throw new BridgeException("BLE scan could not start");
                }

                Task timeoutTask = Task.Delay(ScanTimeout, cancellationToken);
                Task completedTask = await Task.WhenAny(completion.Task, timeoutTask).ConfigureAwait(false);

                if (completedTask != completion.Task)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    throw new BridgeException("compatible screen was not found");
                }

                ScanOutcome outcome = await completion.Task.ConfigureAwait(false);
                if (!outcome.HasAddress)
                {
                    throw new BridgeException("BLE scan stopped unexpectedly");
                }

                return outcome;
            }
            finally
            {
                try
                {
                    watcher.Received -= receivedHandler;
                }
                catch (Exception)
                {
                }

                try
                {
                    watcher.Stopped -= stoppedHandler;
                }
                catch (Exception)
                {
                }

                if (watcher.Status == BluetoothLEAdvertisementWatcherStatus.Started)
                {
                    try
                    {
                        watcher.Stop();
                    }
                    catch (Exception)
                    {
                    }
                }
            }
        }

        private static async Task<BleConnection> ConnectAsync(string deviceId, CancellationToken cancellationToken)
        {
            if (string.IsNullOrEmpty(deviceId))
            {
                throw new ArgumentException("Device ID is required.", "deviceId");
            }

            BluetoothLEDevice device = await RunWithTimeoutAsync(
                delegate(CancellationToken operationToken)
                {
                    return ToTask<BluetoothLEDevice>(BluetoothLEDevice.FromIdAsync(deviceId), operationToken);
                },
                ConnectTimeout,
                cancellationToken,
                "paired device open").ConfigureAwait(false);

            if (device == null)
            {
                throw new BridgeException("paired screen is currently unavailable");
            }

            BleConnection connection = null;
            GattSession session = null;
            GattDeviceService selectedService = null;
            bool connectionReady = false;
            bool maintainConnectionRequested = false;

            try
            {
                DeviceInformationPairing pairing = device.DeviceInformation.Pairing;
                if (!pairing.IsPaired)
                {
                    throw new PairingRequiredException("the saved Windows device is no longer paired; stop the bridge, press PAIR on the screen, pair ESP32-AI-Status in Windows Settings, then start the bridge again");
                }

                session = await RunWithTimeoutAsync(
                    delegate(CancellationToken operationToken)
                    {
                        return ToTask<GattSession>(GattSession.FromDeviceIdAsync(device.BluetoothDeviceId), operationToken);
                    },
                    ConnectTimeout,
                    cancellationToken,
                    "GATT session open").ConfigureAwait(false);

                if (session == null)
                {
                    throw new BridgeException("Windows could not create a GATT session for the paired screen");
                }

                connection = new BleConnection(device, session);

                try
                {
                    if (session.CanMaintainConnection)
                    {
                        session.MaintainConnection = true;
                        maintainConnectionRequested = true;
                    }
                    else
                    {
                        WriteLog("Windows cannot maintain this GATT session; continuing with normal reconnect handling");
                    }
                }
                catch (Exception ex)
                {
                    WriteLog("Windows could not enable persistent GATT maintenance; continuing with normal reconnect handling: " + FormatException(ex));
                }

                session = null;

                GattDeviceServicesResult serviceResult = await DiscoverServicesAsync(
                    device,
                    connection,
                    maintainConnectionRequested,
                    cancellationToken).ConfigureAwait(false);

                if (serviceResult == null)
                {
                    throw new BridgeException("service discovery returned no result");
                }
                if (serviceResult.Status != GattCommunicationStatus.Success)
                {
                    Exception failure = CreateGattException("service discovery", serviceResult.Status, serviceResult.ProtocolError);
                    DisposeServices(serviceResult);
                    throw failure;
                }
                if (serviceResult.Services.Count != 1)
                {
                    int count = serviceResult.Services.Count;
                    DisposeServices(serviceResult);
                    if (count == 0)
                    {
                        throw new BridgeException("the paired screen did not expose the AI status GATT service during this discovery attempt");
                    }

                    throw new FatalBridgeException("the paired screen exposes multiple AI status GATT services and cannot be identified safely");
                }

                selectedService = serviceResult.Services[0];

                DeviceAccessStatus accessStatus = await RunWithTimeoutAsync(
                    delegate(CancellationToken operationToken)
                    {
                        return ToTask<DeviceAccessStatus>(selectedService.RequestAccessAsync(), operationToken);
                    },
                    DiscoveryTimeout,
                    cancellationToken,
                    "GATT service access request").ConfigureAwait(false);

                if (accessStatus == DeviceAccessStatus.DeniedBySystem ||
                    accessStatus == DeviceAccessStatus.DeniedByUser)
                {
                    throw new FatalBridgeException("Windows denied access to the AI status GATT service: " + accessStatus, true);
                }
                if (accessStatus != DeviceAccessStatus.Allowed)
                {
                    throw new BridgeException("Windows could not determine access to the AI status GATT service: " + accessStatus);
                }

                GattCharacteristicsResult deviceInfoResult = await DiscoverCharacteristicsAsync(
                    selectedService,
                    DeviceInfoCharacteristicUuid,
                    "device information discovery",
                    cancellationToken).ConfigureAwait(false);

                GattCharacteristicsResult statusResult = await DiscoverCharacteristicsAsync(
                    selectedService,
                    StatusCharacteristicUuid,
                    "status discovery",
                    cancellationToken).ConfigureAwait(false);

                if (deviceInfoResult == null)
                {
                    throw new BridgeException("device information discovery returned no result");
                }
                if (deviceInfoResult.Status != GattCommunicationStatus.Success)
                {
                    throw CreateGattException("device information discovery", deviceInfoResult.Status, deviceInfoResult.ProtocolError);
                }
                if (deviceInfoResult.Characteristics.Count != 1)
                {
                    throw new FatalBridgeException("the paired screen has an incompatible device information characteristic layout");
                }
                if (statusResult == null)
                {
                    throw new BridgeException("status discovery returned no result");
                }
                if (statusResult.Status != GattCommunicationStatus.Success)
                {
                    throw CreateGattException("status discovery", statusResult.Status, statusResult.ProtocolError);
                }
                if (statusResult.Characteristics.Count != 1)
                {
                    throw new FatalBridgeException("the paired screen has an incompatible status characteristic layout");
                }

                GattCharacteristic deviceInfoCharacteristic = deviceInfoResult.Characteristics[0];
                GattCharacteristic statusCharacteristic = statusResult.Characteristics[0];

                if ((deviceInfoCharacteristic.CharacteristicProperties & GattCharacteristicProperties.Read) == 0)
                {
                    throw new FatalBridgeException("the paired screen device information characteristic is not readable");
                }

                if ((statusCharacteristic.CharacteristicProperties & GattCharacteristicProperties.Write) == 0)
                {
                    throw new FatalBridgeException("the paired screen status characteristic does not support acknowledged writes");
                }

                bool isCompatible = await ValidateDeviceInfoAsync(deviceInfoCharacteristic, cancellationToken).ConfigureAwait(false);
                if (!isCompatible)
                {
                    throw new FatalBridgeException("the paired screen uses an incompatible AI status protocol version");
                }

                connection.Activate(selectedService, statusCharacteristic);
                selectedService = null;

                connectionReady = true;
                return connection;
            }
            finally
            {
                if (selectedService != null)
                {
                    SafeDisposeService(selectedService);
                }
                if (!connectionReady)
                {
                    if (connection != null)
                    {
                        connection.Dispose();
                    }
                    else
                    {
                        if (session != null)
                        {
                            try
                            {
                                session.MaintainConnection = false;
                            }
                            catch (Exception)
                            {
                            }

                            try
                            {
                                session.Dispose();
                            }
                            catch (Exception)
                            {
                            }
                        }

                        try
                        {
                            device.Dispose();
                        }
                        catch (Exception)
                        {
                        }
                    }
                }
            }
        }

        private static async Task<GattDeviceServicesResult> DiscoverServicesAsync(BluetoothLEDevice device,
                                                                                    BleConnection connection,
                                                                                    bool maintainConnectionRequested,
                                                                                    CancellationToken cancellationToken)
        {
            GattDeviceServicesResult cachedResult = null;

            try
            {
                cachedResult = await QueryServicesAsync(
                    device,
                    BluetoothCacheMode.Cached,
                    "cached service discovery",
                    cancellationToken).ConfigureAwait(false);
            }
            catch (BridgeException ex)
            {
                WriteLog("cached service lookup was unavailable: " + ex.Message);
            }

            if (cachedResult != null && cachedResult.Status == GattCommunicationStatus.AccessDenied)
            {
                return cachedResult;
            }

            if (cachedResult != null &&
                cachedResult.Status == GattCommunicationStatus.Success &&
                cachedResult.Services.Count > 0)
            {
                if (maintainConnectionRequested)
                {
                    WriteLog("waiting for the paired screen connection");
                    bool connected = await connection.WaitForConnectedAsync(ConnectTimeout, cancellationToken).ConfigureAwait(false);
                    if (!connected)
                    {
                        DisposeServices(cachedResult);
                        throw new BridgeException("the paired screen did not establish a physical GATT connection");
                    }
                }

                return cachedResult;
            }

            DisposeServices(cachedResult);

            if (maintainConnectionRequested)
            {
                WriteLog("waiting for the paired screen connection before refreshing services");
                await connection.WaitForConnectedAsync(ConnectTimeout, cancellationToken).ConfigureAwait(false);
            }

            return await QueryServicesAsync(
                device,
                BluetoothCacheMode.Uncached,
                "service discovery",
                cancellationToken).ConfigureAwait(false);
        }

        private static async Task<GattDeviceServicesResult> QueryServicesAsync(BluetoothLEDevice device,
                                                                                BluetoothCacheMode cacheMode,
                                                                                string operationName,
                                                                                CancellationToken cancellationToken)
        {
            return await RunWithTimeoutAsync(
                delegate(CancellationToken operationToken)
                {
                    return ToTask<GattDeviceServicesResult>(device.GetGattServicesForUuidAsync(ServiceUuid, cacheMode), operationToken);
                },
                DiscoveryTimeout,
                cancellationToken,
                operationName).ConfigureAwait(false);
        }

        private static async Task<GattCharacteristicsResult> DiscoverCharacteristicsAsync(GattDeviceService service,
                                                                                            Guid characteristicUuid,
                                                                                            string operationName,
                                                                                            CancellationToken cancellationToken)
        {
            return await RunWithTimeoutAsync(
                delegate(CancellationToken operationToken)
                {
                    return ToTask<GattCharacteristicsResult>(service.GetCharacteristicsForUuidAsync(characteristicUuid, BluetoothCacheMode.Uncached), operationToken);
                },
                DiscoveryTimeout,
                cancellationToken,
                operationName).ConfigureAwait(false);
        }

        private static void DisposeServices(GattDeviceServicesResult result)
        {
            if (result == null)
            {
                return;
            }

            for (int index = 0; index < result.Services.Count; index++)
            {
                SafeDisposeService(result.Services[index]);
            }
        }

        private static void SafeDisposeService(GattDeviceService service)
        {
            if (service == null)
            {
                return;
            }

            try
            {
                service.Dispose();
            }
            catch (Exception)
            {
            }
        }

        private static Exception CreateGattException(string operationName,
                                                     GattCommunicationStatus status,
                                                     byte? protocolError)
        {
            if (status == GattCommunicationStatus.AccessDenied)
            {
                return new FatalBridgeException(operationName + " was denied by Windows; check Bluetooth privacy and device access settings", true);
            }

            if (status == GattCommunicationStatus.Unreachable)
            {
                return new BridgeException(operationName + " could not reach the screen");
            }

            if (status == GattCommunicationStatus.ProtocolError)
            {
                string protocolText = protocolError.HasValue ? " ATT error 0x" + protocolError.Value.ToString("X2") : " an unspecified ATT error";

                if (!protocolError.HasValue && string.Equals(operationName, "status write", StringComparison.Ordinal))
                {
                    return new PairingRequiredException(operationName + " was rejected by the secured characteristic with" + protocolText + PairingResetHint);
                }

                if (protocolError == 0x05 || protocolError == 0x08 || protocolError == 0x0C || protocolError == 0x0F)
                {
                    return new PairingRequiredException(operationName + " failed authentication with" + protocolText + PairingResetHint);
                }

                return new BridgeException(operationName + " returned" + protocolText);
            }

            return new BridgeException(operationName + " returned " + status);
        }

        private static async Task<bool> ValidateDeviceInfoAsync(GattCharacteristic characteristic, CancellationToken cancellationToken)
        {
            GattReadResult result = await RunWithTimeoutAsync(
                delegate(CancellationToken operationToken)
                {
                    return ToTask<GattReadResult>(characteristic.ReadValueAsync(BluetoothCacheMode.Uncached), operationToken);
                },
                IoTimeout,
                cancellationToken,
                "device information read").ConfigureAwait(false);

            if (result == null)
            {
                throw new BridgeException("device information read returned no result");
            }
            if (result.Status != GattCommunicationStatus.Success)
            {
                throw CreateGattException("device information read", result.Status, result.ProtocolError);
            }
            if (result.Value == null || result.Value.Length != 8)
            {
                uint length = result.Value == null ? 0U : result.Value.Length;
                throw new FatalBridgeException("the paired screen returned an incompatible " + length + "-byte device information frame");
            }

            byte[] data = new byte[8];
            using (DataReader reader = DataReader.FromBuffer(result.Value))
            {
                reader.ReadBytes(data);
            }

            for (int index = 0; index < ExpectedDeviceInfoPrefix.Length; index++)
            {
                if (data[index] != ExpectedDeviceInfoPrefix[index])
                {
                    return false;
                }
            }

            return data[7] == ComputeCrc8Atm(data, 7);
        }

        private static async Task PublishLoopAsync(BleConnection connection, CancellationToken cancellationToken)
        {
            ProcessSnapshot previous = null;
            DetailTransmissionState detailTransmission = new DetailTransmissionState();

            while (true)
            {
                cancellationToken.ThrowIfCancellationRequested();

                if (connection.ServicesChanged)
                {
                    throw new BridgeException("the screen GATT service table changed; rebuilding the connection");
                }

                if (connection.NeedsReconnect)
                {
                    bool recovered = await RecoverConnectionAsync(connection, cancellationToken).ConfigureAwait(false);
                    if (recovered)
                    {
                        throw new BridgeException("screen reconnected; rebuilding GATT service references");
                    }

                    throw new BridgeException("screen did not reconnect within " + (int)DisconnectGracePeriod.TotalSeconds + " seconds");
                }

                ProcessSnapshot snapshot = ProcessSnapshots.GetSnapshot();
                byte[] packet = BuildStatusPacket(snapshot);
                byte statusSequence = packet[2];
                GattCommunicationStatus writeStatus;
                BridgeException writeFailure = null;
                try
                {
                    writeStatus = await WriteStatusAsync(connection.StatusCharacteristic, packet, cancellationToken).ConfigureAwait(false);
                }
                catch (BridgeException ex)
                {
                    writeStatus = GattCommunicationStatus.Unreachable;
                    writeFailure = ex;
                }

                if (writeFailure != null)
                {
                    if (!connection.IsDeviceConnected || connection.NeedsReconnect)
                    {
                        connection.MarkDisconnected();
                        bool recovered = await RecoverConnectionAsync(connection, cancellationToken).ConfigureAwait(false);
                        if (recovered)
                        {
                            throw new BridgeException("screen reconnected; rebuilding GATT service references");
                        }

                        throw new BridgeException(writeFailure.Message + "; screen did not reconnect within " + (int)DisconnectGracePeriod.TotalSeconds + " seconds");
                    }

                    throw new BridgeException(writeFailure.Message);
                }

                if (writeStatus != GattCommunicationStatus.Success)
                {
                    if (writeStatus == GattCommunicationStatus.Unreachable)
                    {
                        if (!connection.IsDeviceConnected || connection.NeedsReconnect)
                        {
                            connection.MarkDisconnected();
                            bool recovered = await RecoverConnectionAsync(connection, cancellationToken).ConfigureAwait(false);
                            if (recovered)
                            {
                                throw new BridgeException("screen reconnected; rebuilding GATT service references");
                            }

                            throw new BridgeException("status write could not reach the screen and it did not reconnect within " + (int)DisconnectGracePeriod.TotalSeconds + " seconds");
                        }
                    }

                    throw CreateGattException("status write", writeStatus, null);
                }

                bool detailBatchComplete = await PublishDetailFramesAsync(
                    connection.StatusCharacteristic,
                    snapshot,
                    statusSequence,
                    detailTransmission,
                    cancellationToken).ConfigureAwait(false);

                if (previous == null || !snapshot.HasSameStatus(previous))
                {
                    WriteLog("status codex=" + StateName(snapshot.CodexState) + " claude=" + StateName(snapshot.ClaudeState));
                    previous = snapshot;
                }

                await Task.Delay(
                    detailBatchComplete ? HeartbeatInterval : DetailFailureRetryDelay,
                    cancellationToken).ConfigureAwait(false);
            }
        }

        private static async Task<bool> RecoverConnectionAsync(BleConnection connection, CancellationToken cancellationToken)
        {
            if (connection.ServicesChanged)
            {
                return false;
            }

            WriteLog("screen connection was interrupted; waiting for Windows to reconnect it");
            bool recovered = await connection.WaitForConnectedAsync(DisconnectGracePeriod, cancellationToken).ConfigureAwait(false);

            if (recovered)
            {
                WriteLog("screen connection recovered");
            }

            return recovered;
        }

        private static async Task<GattCommunicationStatus> WriteStatusAsync(GattCharacteristic characteristic, byte[] packet, CancellationToken cancellationToken)
        {
            return await WritePacketAsync(
                characteristic,
                packet,
                IoTimeout,
                "status write",
                cancellationToken).ConfigureAwait(false);
        }

        private static async Task<GattCommunicationStatus> WriteDetailAsync(GattCharacteristic characteristic, byte[] packet, CancellationToken cancellationToken)
        {
            return await WritePacketAsync(
                characteristic,
                packet,
                DetailIoTimeout,
                "detail write",
                cancellationToken).ConfigureAwait(false);
        }

        private static async Task<GattCommunicationStatus> WritePacketAsync(GattCharacteristic characteristic,
                                                                              byte[] packet,
                                                                              TimeSpan timeout,
                                                                              string operationName,
                                                                              CancellationToken cancellationToken)
        {
            IBuffer buffer;
            using (DataWriter writer = new DataWriter())
            {
                writer.WriteBytes(packet);
                buffer = writer.DetachBuffer();
            }

            return await RunWithTimeoutAsync(
                delegate(CancellationToken operationToken)
                {
                    return ToTask<GattCommunicationStatus>(characteristic.WriteValueAsync(buffer, GattWriteOption.WriteWithResponse), operationToken);
                },
                timeout,
                cancellationToken,
                operationName).ConfigureAwait(false);
        }

        private static async Task<bool> PublishDetailFramesAsync(GattCharacteristic characteristic,
                                                                 ProcessSnapshot snapshot,
                                                                 byte statusSequence,
                                                                 DetailTransmissionState transmission,
                                                                 CancellationToken cancellationToken)
        {
            long lastHeartbeatTimestamp = Stopwatch.GetTimestamp();

            if (!transmission.Enabled)
            {
                if (DateTime.UtcNow < transmission.RetryAfterUtc)
                {
                    return true;
                }

                transmission.Enabled = true;
                transmission.PreviousCount = -1;
                WriteLog("retrying detail transmission after cooldown");
            }

            transmission.PrepareBatch(snapshot.DetailRecords.Length);
            int count = Math.Min(snapshot.DetailRecords.Length, transmission.RecordLimit);
            if (count == 0)
            {
                if (transmission.PreviousCount != 0)
                {
                    byte[] clearFrame = BuildDetailPacket(statusSequence, 0xFF, 0, 0, null);
                    GattCommunicationStatus? clearStatus = await TryWriteDetailAsync(characteristic, clearFrame, cancellationToken).ConfigureAwait(false);
                    if (!HandleDetailWriteStatus(clearStatus, transmission, 0))
                    {
                        return false;
                    }
                }

                transmission.PreviousCount = 0;
                transmission.CompleteBatch(0);
                return true;
            }

            for (int index = 0; index < count; index++)
            {
                byte[] detailFrame = BuildDetailPacket(
                    statusSequence,
                    (byte)index,
                    count,
                    snapshot.TotalDetailCount,
                    snapshot.DetailRecords[index]);
                GattCommunicationStatus? detailStatus = await TryWriteDetailAsync(characteristic, detailFrame, cancellationToken).ConfigureAwait(false);
                if (!HandleDetailWriteStatus(detailStatus, transmission, count))
                {
                    return false;
                }

                if (StopwatchElapsedMilliseconds(lastHeartbeatTimestamp, Stopwatch.GetTimestamp()) >=
                    HeartbeatInterval.TotalMilliseconds)
                {
                    ProcessSnapshot heartbeatSnapshot = ProcessSnapshots.GetSnapshot();
                    byte[] heartbeatPacket = BuildStatusPacket(heartbeatSnapshot, statusSequence);
                    GattCommunicationStatus heartbeatStatus = await WriteStatusAsync(
                        characteristic,
                        heartbeatPacket,
                        cancellationToken).ConfigureAwait(false);
                    if (heartbeatStatus != GattCommunicationStatus.Success)
                    {
                        throw CreateGattException("status write", heartbeatStatus, null);
                    }

                    lastHeartbeatTimestamp = Stopwatch.GetTimestamp();
                }
            }

            transmission.PreviousCount = count;
            if (transmission.CompleteBatch(count))
            {
                WriteLog("detail transmission recovered to six records");
            }

            return true;
        }

        private static async Task<GattCommunicationStatus?> TryWriteDetailAsync(GattCharacteristic characteristic,
                                                                                 byte[] packet,
                                                                                 CancellationToken cancellationToken)
        {
            try
            {
                return await WriteDetailAsync(characteristic, packet, cancellationToken).ConfigureAwait(false);
            }
            catch (BridgeException ex)
            {
                WriteLog("detail write failed; aggregate status remains enabled: " + ex.Message);
                return null;
            }
        }

        private static bool HandleDetailWriteStatus(GattCommunicationStatus? status,
                                                    DetailTransmissionState transmission,
                                                    int recordCount)
        {
            if (!status.HasValue)
            {
                return false;
            }
            if (status.Value == GattCommunicationStatus.Success)
            {
                return true;
            }
            if (status.Value == GattCommunicationStatus.ProtocolError)
            {
                if (recordCount > 3)
                {
                    transmission.FallbackToThree();
                    WriteLog("screen rejected more than three detail records; using three before a delayed six-record recovery probe");
                    return false;
                }

                transmission.Suspend();
                WriteLog("screen rejected detail frames; retrying after a 30-second cooldown");
                return false;
            }

            WriteLog("detail write returned " + status.Value + "; aggregate status remains enabled");
            return false;
        }

        private static byte[] BuildDetailPacket(byte statusSequence,
                                                byte index,
                                                int count,
                                                int total,
                                                DetailRecord record)
        {
            byte[] packet = new byte[20];
            packet[0] = 0xA2;
            packet[1] = 0x01;
            packet[2] = statusSequence;
            packet[3] = index;

            if (record != null)
            {
                packet[4] = (byte)Math.Min(count, 255);
                packet[5] = (byte)Math.Min(total, 255);
                packet[6] = record.Product;
                packet[7] = record.State;
                packet[8] = (byte)record.Label.Length;

                for (int labelIndex = 0; labelIndex < record.Label.Length && labelIndex < 10; labelIndex++)
                {
                    packet[9 + labelIndex] = (byte)record.Label[labelIndex];
                }
            }

            packet[19] = ComputeCrc8Atm(packet, 19);
            return packet;
        }

        private static byte[] BuildStatusPacket(ProcessSnapshot snapshot)
        {
            byte packetSequence = sequence;
            sequence = unchecked((byte)(sequence + 1));
            return BuildStatusPacket(snapshot, packetSequence);
        }

        private static byte[] BuildStatusPacket(ProcessSnapshot snapshot, byte packetSequence)
        {
            byte[] packet = new byte[8];
            packet[0] = 0xA1;
            packet[1] = 0x01;
            packet[2] = packetSequence;
            packet[3] = snapshot.CodexState;
            packet[4] = snapshot.ClaudeState;
            packet[5] = snapshot.Flags;
            packet[6] = 0x00;
            packet[7] = ComputeCrc8Atm(packet, 7);
            return packet;
        }

        private static double StopwatchElapsedMilliseconds(long startTimestamp, long endTimestamp)
        {
            if (startTimestamp <= 0 || endTimestamp < startTimestamp)
            {
                return double.MaxValue;
            }

            return (endTimestamp - startTimestamp) * 1000.0 / Stopwatch.Frequency;
        }

        private static byte ComputeCrc8Atm(byte[] data, int length)
        {
            byte crc = 0;

            for (int index = 0; index < length; index++)
            {
                crc ^= data[index];

                for (int bit = 0; bit < 8; bit++)
                {
                    if ((crc & 0x80) != 0)
                    {
                        crc = unchecked((byte)((crc << 1) ^ 0x07));
                    }
                    else
                    {
                        crc = unchecked((byte)(crc << 1));
                    }
                }
            }

            return crc;
        }

        private static Task<T> ToTask<T>(IAsyncOperation<T> operation, CancellationToken cancellationToken)
        {
            if (operation == null)
            {
                throw new ArgumentNullException("operation");
            }

            TaskCompletionSource<T> completion = new TaskCompletionSource<T>(TaskCreationOptions.RunContinuationsAsynchronously);

            try
            {
                operation.Completed = delegate(IAsyncOperation<T> asyncOperation, AsyncStatus status)
                {
                    try
                    {
                        if (status == AsyncStatus.Completed)
                        {
                            completion.TrySetResult(asyncOperation.GetResults());
                        }
                        else if (status == AsyncStatus.Canceled)
                        {
                            completion.TrySetCanceled();
                        }
                        else if (status == AsyncStatus.Error)
                        {
                            completion.TrySetException(asyncOperation.ErrorCode ?? new InvalidOperationException("WinRT operation failed"));
                        }
                        else
                        {
                            completion.TrySetException(new InvalidOperationException("WinRT operation completed with an invalid status"));
                        }
                    }
                    catch (Exception ex)
                    {
                        completion.TrySetException(ex);
                    }
                    finally
                    {
                        try
                        {
                            asyncOperation.Close();
                        }
                        catch (Exception)
                        {
                        }
                    }
                };
            }
            catch
            {
                try
                {
                    operation.Cancel();
                }
                catch (Exception)
                {
                }
                try
                {
                    operation.Close();
                }
                catch (Exception)
                {
                }
                throw;
            }

            if (cancellationToken.CanBeCanceled)
            {
                cancellationToken.Register(
                    delegate
                    {
                        try
                        {
                            operation.Cancel();
                        }
                        catch (Exception)
                        {
                        }
                    });
            }

            return completion.Task;
        }

        private static async Task<T> RunWithTimeoutAsync<T>(Func<CancellationToken, Task<T>> operation, TimeSpan timeout, CancellationToken cancellationToken, string operationName)
        {
            cancellationToken.ThrowIfCancellationRequested();

            using (CancellationTokenSource operationCancellation = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken))
            {
                Task<T> operationTask;

                try
                {
                    operationTask = operation(operationCancellation.Token);
                }
                catch (Exception ex)
                {
                    if (cancellationToken.IsCancellationRequested)
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                    }

                    throw new BridgeException(operationName + " failed: " + FormatException(ex));
                }

                Task timeoutTask = Task.Delay(timeout, cancellationToken);
                Task completedTask = await Task.WhenAny(operationTask, timeoutTask).ConfigureAwait(false);

                if (completedTask != operationTask)
                {
                    try
                    {
                        operationCancellation.Cancel();
                    }
                    catch (Exception)
                    {
                    }
                    ObserveAbandonedTask(operationTask);
                    cancellationToken.ThrowIfCancellationRequested();
                    throw new BridgeException(operationName + " timed out");
                }

                if (cancellationToken.IsCancellationRequested)
                {
                    ObserveAbandonedTask(operationTask);
                    cancellationToken.ThrowIfCancellationRequested();
                }

                try
                {
                    return await operationTask.ConfigureAwait(false);
                }
                catch (OperationCanceledException ex)
                {
                    if (cancellationToken.IsCancellationRequested)
                    {
                        throw;
                    }

                    throw new BridgeException(operationName + " failed: " + FormatException(ex));
                }
                catch (Exception ex)
                {
                    throw new BridgeException(operationName + " failed: " + FormatException(ex));
                }
            }
        }

        private static void ObserveAbandonedTask<T>(Task<T> task)
        {
            task.ContinueWith(
                delegate(Task<T> completedTask)
                {
                    if (completedTask.IsFaulted)
                    {
                        AggregateException ignored = completedTask.Exception;
                        return;
                    }

                    if (completedTask.Status == TaskStatus.RanToCompletion)
                    {
                        object result = completedTask.Result;
                        GattDeviceServicesResult servicesResult = result as GattDeviceServicesResult;
                        if (servicesResult != null)
                        {
                            try
                            {
                                DisposeServices(servicesResult);
                            }
                            catch (Exception)
                            {
                            }
                            return;
                        }

                        IDisposable disposable = result as IDisposable;
                        if (disposable != null)
                        {
                            try
                            {
                                disposable.Dispose();
                            }
                            catch (Exception)
                            {
                            }
                        }
                    }
                },
                CancellationToken.None,
                TaskContinuationOptions.ExecuteSynchronously,
                TaskScheduler.Default);
        }

        private static string StateName(byte state)
        {
            if (state == StateOff)
            {
                return "off";
            }

            if (state == StateIdle)
            {
                return "idle";
            }

            if (state == StateBusy)
            {
                return "busy";
            }

            return "unknown";
        }

        private static byte ResolveCodexState(ProcessActivityReading cpu, CodexTranscriptSnapshot transcript)
        {
            if (cpu.PresenceMask == 0)
            {
                return StateOff;
            }

            bool snapshotFresh = transcript != null &&
                                 DateTime.UtcNow - transcript.CreatedUtc <= TimeSpan.FromSeconds(6);
            if (!snapshotFresh)
            {
                return cpu.State;
            }
            if (!transcript.IsHealthy)
            {
                return cpu.State == StateBusy ? StateBusy : StateUnknown;
            }
            if (transcript.AnyBusy)
            {
                return StateBusy;
            }
            if (transcript.AnyWaiting)
            {
                return cpu.State == StateBusy ? StateBusy : StateIdle;
            }
            if (transcript.AnyStaleActive)
            {
                return cpu.State == StateBusy ? StateBusy : StateUnknown;
            }
            if (transcript.HasReliableBoundary && transcript.AllReliableSessionsInactive)
            {
                return StateIdle;
            }

            return cpu.State;
        }

        private static DetailRecord[] BuildDetailRecords(CodexTranscriptSnapshot transcript,
                                                         byte codexState)
        {
            List<DetailRecord> records = new List<DetailRecord>();

            if (transcript != null && transcript.IsHealthy)
            {
                for (int index = 0; index < transcript.Summaries.Length && records.Count < 6; index++)
                {
                    SessionSummary summary = transcript.Summaries[index];
                    if (codexState != StateOff ||
                        summary.State == DetailDone ||
                        summary.State == DetailFailed)
                    {
                        records.Add(new DetailRecord(ProductCodex, summary.State, summary.Label));
                    }
                }
            }

            if (records.Count == 0)
            {
                records.Add(new DetailRecord(ProductCodex, DetailStateFromAggregate(codexState), "CODEX APP"));
            }

            return records.ToArray();
        }

        private static byte DetailStateFromAggregate(byte state)
        {
            if (state == StateBusy)
            {
                return DetailBusy;
            }
            if (state == StateIdle)
            {
                return DetailIdle;
            }
            if (state == StateOff)
            {
                return DetailOff;
            }

            return DetailStale;
        }

        private static string FormatException(Exception exception)
        {
            return exception.GetType().Name + " HRESULT=0x" + exception.HResult.ToString("X8") + ": " + exception.Message;
        }

        private static void WriteLog(string message)
        {
            Console.WriteLine("{0:yyyy-MM-ddTHH:mm:ss.fffZ} {1}", DateTime.UtcNow, message);
        }

        private sealed class ProcessSnapshot
        {
            public byte CodexState { get; private set; }
            public byte ClaudeState { get; private set; }
            public byte Flags { get; private set; }
            public DetailRecord[] DetailRecords { get; private set; }
            public int TotalDetailCount { get; private set; }

            private ProcessSnapshot(byte codexState,
                                    byte claudeState,
                                    byte flags,
                                    DetailRecord[] detailRecords,
                                    int totalDetailCount)
            {
                CodexState = codexState;
                ClaudeState = claudeState;
                Flags = flags;
                DetailRecords = detailRecords;
                TotalDetailCount = totalDetailCount;
            }

            public static ProcessSnapshot Capture()
            {
                int currentSessionId = -1;

                try
                {
                    using (Process current = Process.GetCurrentProcess())
                    {
                        currentSessionId = current.SessionId;
                    }
                }
                catch (Exception)
                {
                    return Unknown();
                }

                ProcessActivityReading codex = CodexActivityDetector.Capture(currentSessionId);
                ProcessActivityReading claude = ClaudeActivityDetector.Capture(currentSessionId);
                CodexTranscriptSnapshot transcript = CodexTranscripts.GetSnapshot();
                byte codexState = codex.IsReliable ? ResolveCodexState(codex, transcript) : StateUnknown;
                byte claudeState = StateUnknown;

                if (claude.IsReliable)
                {
                    if ((claude.PresenceMask & 0x01) == 0)
                    {
                        ClaudeDesktopLogs.Capture(false);
                        claudeState = StateOff;
                    }
                    else
                    {
                        ClaudeDesktopLogReading claudeLog = ClaudeDesktopLogs.Capture(true);
                        if (!claudeLog.IsCompatible)
                        {
                            claudeState = claude.State;
                        }
                        else if (!claudeLog.IsReliable)
                        {
                            claudeState = StateUnknown;
                        }
                        else if (claudeLog.State == StateBusy)
                        {
                            claudeState = StateBusy;
                        }
                        else if (claudeLog.SuppressCpu)
                        {
                            ClaudeActivityDetector.SuppressActivity();
                            claudeState = StateIdle;
                        }
                        else
                        {
                            claudeState = claude.State;
                        }
                    }
                }

                byte flags = 0;

                if (codex.IsReliable || claude.IsReliable)
                {
                    flags |= 0x01;
                }

                if ((codex.PresenceMask & 0x01) != 0)
                {
                    flags |= 0x02;
                }

                if ((codex.PresenceMask & 0x02) != 0)
                {
                    flags |= 0x04;
                }

                if ((claude.PresenceMask & 0x01) != 0)
                {
                    flags |= 0x08;
                }

                DetailRecord[] detailRecords = BuildDetailRecords(transcript, codexState);
                int totalDetailCount = codexState != StateOff &&
                                       transcript != null &&
                                       transcript.IsHealthy &&
                                       transcript.Summaries.Length > 0
                    ? Math.Min(255, transcript.TotalSessions)
                    : detailRecords.Length;

                return new ProcessSnapshot(codexState, claudeState, flags, detailRecords, totalDetailCount);
            }

            public static ProcessSnapshot Unknown()
            {
                return new ProcessSnapshot(
                    StateUnknown,
                    StateUnknown,
                    0,
                    new DetailRecord[] {
                        new DetailRecord(ProductCodex, DetailStale, "CODEX APP")
                    },
                    1);
            }

            public bool HasSameStatus(ProcessSnapshot other)
            {
                return CodexState == other.CodexState && ClaudeState == other.ClaudeState && Flags == other.Flags;
            }
        }

        private sealed class ProcessSnapshotMonitor : IDisposable
        {
            private static readonly TimeSpan SampleInterval = TimeSpan.FromSeconds(1);
            private static readonly TimeSpan MaximumSampleAge = TimeSpan.FromSeconds(5);
            private readonly object sync = new object();
            private readonly ManualResetEvent stopEvent = new ManualResetEvent(false);
            private Thread worker;
            private ProcessSnapshot snapshot;
            private long snapshotTimestamp;
            private bool started;
            private bool disposed;

            public void Start()
            {
                lock (sync)
                {
                    if (disposed || started)
                    {
                        return;
                    }

                    worker = new Thread(Run);
                    worker.IsBackground = true;
                    worker.Name = "AI process state monitor";

                    try
                    {
                        worker.Start();
                        started = true;
                    }
                    catch
                    {
                        worker = null;
                        throw;
                    }
                }
            }

            public ProcessSnapshot GetSnapshot()
            {
                long nowTimestamp = Stopwatch.GetTimestamp();

                lock (sync)
                {
                    if (snapshot != null &&
                        StopwatchElapsedMilliseconds(snapshotTimestamp, nowTimestamp) <= MaximumSampleAge.TotalMilliseconds)
                    {
                        return snapshot;
                    }
                }

                return ProcessSnapshot.Unknown();
            }

            private void Run()
            {
                while (!stopEvent.WaitOne(0))
                {
                    ProcessSnapshot value;

                    try
                    {
                        value = ProcessSnapshot.Capture();
                    }
                    catch (Exception)
                    {
                        value = ProcessSnapshot.Unknown();
                    }

                    lock (sync)
                    {
                        snapshot = value;
                        snapshotTimestamp = Stopwatch.GetTimestamp();
                    }

                    if (stopEvent.WaitOne((int)SampleInterval.TotalMilliseconds))
                    {
                        break;
                    }
                }
            }

            public void Dispose()
            {
                Thread thread;

                lock (sync)
                {
                    if (disposed)
                    {
                        return;
                    }

                    disposed = true;
                    stopEvent.Set();
                    thread = worker;
                }

                if (thread != null && thread != Thread.CurrentThread)
                {
                    thread.Join(2000);
                }
            }
        }

        private sealed class DetailRecord
        {
            public byte Product { get; private set; }
            public byte State { get; private set; }
            public string Label { get; private set; }

            public DetailRecord(byte product, byte state, string label)
            {
                Product = product;
                State = state;
                Label = NormalizeLabel(label);
            }

            private static string NormalizeLabel(string value)
            {
                if (string.IsNullOrEmpty(value))
                {
                    return "CODEX";
                }

                StringBuilder normalized = new StringBuilder(10);
                for (int index = 0; index < value.Length && normalized.Length < 10; index++)
                {
                    char character = value[index];
                    if (character >= 0x20 && character <= 0x7E)
                    {
                        normalized.Append(character);
                    }
                }

                string result = normalized.ToString().Trim();
                return result.Length == 0 ? "CODEX" : result;
            }
        }

        private sealed class DetailTransmissionState
        {
            public bool Enabled { get; set; }
            public int PreviousCount { get; set; }
            public int RecordLimit { get; set; }
            public int PreferredRecordLimit { get; set; }
            public DateTime RetryAfterUtc { get; private set; }
            private DateTime fullLimitRetryAfterUtc;
            private bool recoveryProbe;
            private bool fullLimitProbe;
            private bool waitingForStableThree;

            public DetailTransmissionState()
            {
                Enabled = true;
                PreviousCount = -1;
                RecordLimit = 6;
                PreferredRecordLimit = 6;
                RetryAfterUtc = DateTime.MinValue;
                fullLimitRetryAfterUtc = DateTime.MinValue;
            }

            public void PrepareBatch(int availableRecords)
            {
                if (!recoveryProbe &&
                    !fullLimitProbe &&
                    !waitingForStableThree &&
                    PreferredRecordLimit == 3 &&
                    availableRecords > 3 &&
                    fullLimitRetryAfterUtc != DateTime.MinValue &&
                    DateTime.UtcNow >= fullLimitRetryAfterUtc)
                {
                    RecordLimit = 6;
                    fullLimitProbe = true;
                }
            }

            public void Suspend()
            {
                Enabled = false;
                PreviousCount = -1;
                RecordLimit = 1;
                RetryAfterUtc = DateTime.UtcNow + TimeSpan.FromSeconds(30);
                recoveryProbe = true;

                if (PreferredRecordLimit == 3)
                {
                    waitingForStableThree = true;
                    fullLimitRetryAfterUtc = DateTime.MinValue;
                }
            }

            public void FallbackToThree()
            {
                PreferredRecordLimit = 3;
                RecordLimit = 3;
                PreviousCount = -1;
                fullLimitProbe = false;
                waitingForStableThree = true;
                fullLimitRetryAfterUtc = DateTime.MinValue;
            }

            public bool CompleteBatch(int recordCount)
            {
                if (recoveryProbe)
                {
                    recoveryProbe = false;
                    RecordLimit = PreferredRecordLimit;
                    RetryAfterUtc = DateTime.MinValue;
                    return false;
                }

                if (fullLimitProbe)
                {
                    fullLimitProbe = false;
                    waitingForStableThree = false;
                    PreferredRecordLimit = 6;
                    RecordLimit = 6;
                    fullLimitRetryAfterUtc = DateTime.MinValue;
                    return true;
                }

                if (PreferredRecordLimit == 3 && waitingForStableThree && recordCount >= 3)
                {
                    waitingForStableThree = false;
                    fullLimitRetryAfterUtc = DateTime.UtcNow + TimeSpan.FromSeconds(30);
                }

                return false;
            }
        }

        private sealed class ProcessActivityReading
        {
            public byte State { get; private set; }
            public int PresenceMask { get; private set; }
            public bool IsReliable { get; private set; }

            public ProcessActivityReading(byte state, int presenceMask, bool isReliable)
            {
                State = state;
                PresenceMask = presenceMask;
                IsReliable = isReliable;
            }
        }

        private sealed class ClaudeDesktopLogReading
        {
            public byte State { get; private set; }
            public bool IsCompatible { get; private set; }
            public bool IsReliable { get; private set; }
            public bool SuppressCpu { get; private set; }

            public ClaudeDesktopLogReading(byte state, bool isCompatible, bool isReliable)
                : this(state, isCompatible, isReliable, false)
            {
            }

            public ClaudeDesktopLogReading(
                byte state,
                bool isCompatible,
                bool isReliable,
                bool suppressCpu)
            {
                State = state;
                IsCompatible = isCompatible;
                IsReliable = isReliable;
                SuppressCpu = suppressCpu;
            }
        }

        private sealed class ClaudeDesktopLogMonitor
        {
            private const long MaximumInitialBytes = 8L * 1024L * 1024L;
            private const int MaximumPendingCharacters = 8192;
            private static readonly TimeSpan DiscoveryInterval = TimeSpan.FromSeconds(30);
            private static readonly TimeSpan ActiveSessionLeaseTimeout = TimeSpan.FromSeconds(90);
            private static readonly TimeSpan FocusCpuSuppression = TimeSpan.FromSeconds(8);
            private static readonly TimeSpan LaunchCpuSuppression = TimeSpan.FromSeconds(12);
            private static readonly TimeSpan LifecycleSettleCpuSuppression = TimeSpan.FromSeconds(4);
            private readonly Dictionary<string, DateTime> activeSessions =
                new Dictionary<string, DateTime>(StringComparer.Ordinal);
            private string logPath;
            private string pendingLine = string.Empty;
            private long fileOffset;
            private long fileCreationTicks;
            private DateTime nextDiscoveryUtc;
            private DateTime suppressCpuUntilUtc;
            private bool compatible;

            public ClaudeDesktopLogReading Capture(bool processPresent)
            {
                if (!processPresent)
                {
                    activeSessions.Clear();
                    return new ClaudeDesktopLogReading(StateOff, compatible, true);
                }

                try
                {
                    DiscoverLogIfNeeded();

                    if (string.IsNullOrEmpty(logPath))
                    {
                        return new ClaudeDesktopLogReading(StateIdle, false, false);
                    }

                    if (!ReadUpdates())
                    {
                        return new ClaudeDesktopLogReading(StateUnknown, compatible, false);
                    }

                    int freshActiveSessions = CountFreshActiveSessions(DateTime.UtcNow);
                    if (freshActiveSessions > 0)
                    {
                        return new ClaudeDesktopLogReading(StateBusy, compatible, true);
                    }

                    if (activeSessions.Count > 0)
                    {
                        return new ClaudeDesktopLogReading(StateUnknown, compatible, false);
                    }

                    return new ClaudeDesktopLogReading(
                        StateIdle,
                        compatible,
                        true,
                        DateTime.UtcNow < suppressCpuUntilUtc);
                }
                catch (IOException)
                {
                    return new ClaudeDesktopLogReading(StateUnknown, compatible, false);
                }
                catch (UnauthorizedAccessException)
                {
                    return new ClaudeDesktopLogReading(StateUnknown, compatible, false);
                }
                catch (ArgumentException)
                {
                    return new ClaudeDesktopLogReading(StateUnknown, compatible, false);
                }
                catch (NotSupportedException)
                {
                    return new ClaudeDesktopLogReading(StateUnknown, compatible, false);
                }
            }

            private void DiscoverLogIfNeeded()
            {
                DateTime now = DateTime.UtcNow;

                if (!string.IsNullOrEmpty(logPath) &&
                    File.Exists(logPath) &&
                    now < nextDiscoveryUtc)
                {
                    return;
                }

                nextDiscoveryUtc = now + DiscoveryInterval;
                string discoveredPath = FindNewestLogPath();

                if (string.Equals(discoveredPath, logPath, StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }

                logPath = discoveredPath;
                ResetFileState();
            }

            private static string FindNewestLogPath()
            {
                HashSet<string> candidates = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                string localAppData = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData);
                string appData = Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData);

                AddProfileCandidates(candidates, localAppData);
                AddProfileCandidates(candidates, appData);

                if (!string.IsNullOrEmpty(localAppData))
                {
                    string packagesRoot = Path.Combine(localAppData, "Packages");

                    try
                    {
                        if (Directory.Exists(packagesRoot))
                        {
                            string[] packageDirectories = Directory.GetDirectories(
                                packagesRoot,
                                "Claude_*",
                                SearchOption.TopDirectoryOnly);

                            for (int index = 0; index < packageDirectories.Length; index++)
                            {
                                string roamingRoot = Path.Combine(
                                    packageDirectories[index],
                                    "LocalCache",
                                    "Roaming");
                                AddProfileCandidates(candidates, roamingRoot);
                            }
                        }
                    }
                    catch (IOException)
                    {
                    }
                    catch (UnauthorizedAccessException)
                    {
                    }
                }

                string newestPath = null;
                DateTime newestWriteTime = DateTime.MinValue;

                foreach (string candidate in candidates)
                {
                    try
                    {
                        FileInfo info = new FileInfo(candidate);
                        if (info.Exists && (newestPath == null || info.LastWriteTimeUtc > newestWriteTime))
                        {
                            newestPath = info.FullName;
                            newestWriteTime = info.LastWriteTimeUtc;
                        }
                    }
                    catch (IOException)
                    {
                    }
                    catch (UnauthorizedAccessException)
                    {
                    }
                    catch (ArgumentException)
                    {
                    }
                    catch (NotSupportedException)
                    {
                    }
                }

                return newestPath;
            }

            private static void AddProfileCandidates(HashSet<string> candidates, string root)
            {
                if (string.IsNullOrEmpty(root))
                {
                    return;
                }

                candidates.Add(Path.Combine(root, "Claude-3p", "logs", "main.log"));
                candidates.Add(Path.Combine(root, "Claude", "logs", "main.log"));
            }

            private bool ReadUpdates()
            {
                FileInfo info = new FileInfo(logPath);
                info.Refresh();

                if (!info.Exists)
                {
                    return false;
                }

                long creationTicks = info.CreationTimeUtc.Ticks;
                if (info.Length < fileOffset ||
                    (fileCreationTicks != 0 && creationTicks != fileCreationTicks))
                {
                    ResetFileState();
                }

                fileCreationTicks = creationTicks;
                long startOffset = fileOffset;
                bool discardPartialFirstLine = false;

                if (startOffset == 0 && info.Length > MaximumInitialBytes)
                {
                    startOffset = info.Length - MaximumInitialBytes;
                    discardPartialFirstLine = true;
                }

                long availableBytes = info.Length - startOffset;
                if (availableBytes <= 0)
                {
                    return true;
                }

                if (availableBytes > MaximumInitialBytes)
                {
                    availableBytes = MaximumInitialBytes;
                }

                byte[] buffer = new byte[(int)availableBytes];
                int totalRead = 0;

                using (FileStream stream = new FileStream(
                    logPath,
                    FileMode.Open,
                    FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete))
                {
                    stream.Position = startOffset;

                    while (totalRead < buffer.Length)
                    {
                        int read = stream.Read(buffer, totalRead, buffer.Length - totalRead);
                        if (read <= 0)
                        {
                            break;
                        }

                        totalRead += read;
                    }
                }

                fileOffset = startOffset + totalRead;
                if (totalRead == 0)
                {
                    return true;
                }

                string chunk = Encoding.UTF8.GetString(buffer, 0, totalRead);
                if (discardPartialFirstLine)
                {
                    int firstNewline = chunk.IndexOf('\n');
                    chunk = firstNewline >= 0 ? chunk.Substring(firstNewline + 1) : string.Empty;
                }

                string combined = pendingLine + chunk;
                int lastNewline = combined.LastIndexOf('\n');

                if (lastNewline < 0)
                {
                    pendingLine = combined.Length <= MaximumPendingCharacters
                        ? combined
                        : combined.Substring(combined.Length - MaximumPendingCharacters);
                    return true;
                }

                string completeText = combined.Substring(0, lastNewline);
                pendingLine = combined.Substring(lastNewline + 1);
                string[] lines = completeText.Split('\n');

                for (int index = 0; index < lines.Length; index++)
                {
                    ProcessLine(lines[index].TrimEnd('\r'));
                }

                return true;
            }

            private void ProcessLine(string line)
            {
                DateTime lineTimestampUtc = ParseLineTimestampUtc(line);

                if (line.IndexOf("Starting app {", StringComparison.Ordinal) >= 0)
                {
                    activeSessions.Clear();
                    compatible = true;
                    ExtendCpuSuppression(lineTimestampUtc, LaunchCpuSuppression);
                    return;
                }

                if (line.IndexOf("beforeQuit:", StringComparison.Ordinal) >= 0)
                {
                    activeSessions.Clear();
                    compatible = true;
                    return;
                }

                if (line.IndexOf("[SkillsPlugin] Window focused", StringComparison.Ordinal) >= 0)
                {
                    compatible = true;
                    ExtendCpuSuppression(lineTimestampUtc, FocusCpuSuppression);
                    return;
                }

                const string marker = "[Lifecycle] Session ";
                int markerIndex = line.IndexOf(marker, StringComparison.Ordinal);
                if (markerIndex < 0)
                {
                    return;
                }

                int sessionStart = markerIndex + marker.Length;
                int separator = line.IndexOf(": ", sessionStart, StringComparison.Ordinal);
                if (separator <= sessionStart)
                {
                    return;
                }

                string sessionId = line.Substring(sessionStart, separator - sessionStart);
                string transition = line.Substring(separator + 2);
                int idleIndex = transition.IndexOf("idle", StringComparison.Ordinal);
                int initializingIndex = transition.IndexOf("initializing", StringComparison.Ordinal);
                int runningIndex = transition.IndexOf("running", StringComparison.Ordinal);
                int stoppingIndex = transition.IndexOf("stopping", StringComparison.Ordinal);

                if (idleIndex == 0 &&
                    (initializingIndex > idleIndex || runningIndex > idleIndex))
                {
                    activeSessions[sessionId] = lineTimestampUtc;
                    compatible = true;
                }
                else if (idleIndex > 0 &&
                         ((initializingIndex >= 0 && initializingIndex < idleIndex) ||
                          (runningIndex >= 0 && runningIndex < idleIndex) ||
                          (stoppingIndex >= 0 && stoppingIndex < idleIndex)))
                {
                    activeSessions.Remove(sessionId);
                    compatible = true;
                    ExtendCpuSuppression(lineTimestampUtc, LifecycleSettleCpuSuppression);
                }
            }

            private void ExtendCpuSuppression(DateTime eventTimestampUtc, TimeSpan duration)
            {
                DateTime candidate = eventTimestampUtc + duration;
                if (candidate > suppressCpuUntilUtc)
                {
                    suppressCpuUntilUtc = candidate;
                }
            }

            private int CountFreshActiveSessions(DateTime nowUtc)
            {
                int freshCount = 0;
                List<string> expiredSessions = null;

                foreach (KeyValuePair<string, DateTime> session in activeSessions)
                {
                    TimeSpan age = nowUtc - session.Value;

                    if (age <= ActiveSessionLeaseTimeout)
                    {
                        freshCount++;
                    }
                    else
                    {
                        if (expiredSessions == null)
                        {
                            expiredSessions = new List<string>();
                        }

                        expiredSessions.Add(session.Key);
                    }
                }

                if (expiredSessions != null)
                {
                    for (int index = 0; index < expiredSessions.Count; index++)
                    {
                        activeSessions.Remove(expiredSessions[index]);
                    }
                }

                return freshCount;
            }

            private static DateTime ParseLineTimestampUtc(string line)
            {
                DateTime parsed;

                if (line.Length >= 19 &&
                    DateTime.TryParseExact(
                        line.Substring(0, 19),
                        "yyyy-MM-dd HH:mm:ss",
                        CultureInfo.InvariantCulture,
                        DateTimeStyles.AssumeLocal,
                        out parsed))
                {
                    return parsed.ToUniversalTime();
                }

                return DateTime.UtcNow;
            }

            private void ResetFileState()
            {
                activeSessions.Clear();
                pendingLine = string.Empty;
                fileOffset = 0;
                fileCreationTicks = 0;
                suppressCpuUntilUtc = DateTime.MinValue;
                compatible = false;
            }
        }

        private sealed class ProcessActivityDetector
        {
            private const double BusyEnterRatio = 0.10;
            private const double BusyImmediateRatio = 0.30;
            private const double BusyStrongRatio = 0.07;
            private const double BusyExitRatio = 0.05;
            private const double EmaAlpha = 0.50;
            private const double MinimumSampleMilliseconds = 250.0;
            private const double MaximumSampleMilliseconds = 8000.0;
            private const double BusyHoldMilliseconds = 8000.0;
            private const int BusySamplesRequired = 2;
            private const int QuietSamplesRequired = 3;

            private readonly string[] processNames;
            private readonly bool allowImmediateBusy;
            private Dictionary<string, double> previousCpuMilliseconds;
            private long previousTimestamp;
            private long lastStrongActivityTimestamp;
            private byte stableState;
            private double usageEma;
            private bool usageEmaValid;
            private int busySamples;
            private int quietSamples;

            public ProcessActivityDetector(string[] processNames)
                : this(processNames, true)
            {
            }

            public ProcessActivityDetector(string[] processNames, bool allowImmediateBusy)
            {
                this.processNames = processNames;
                this.allowImmediateBusy = allowImmediateBusy;
                previousCpuMilliseconds = new Dictionary<string, double>(StringComparer.Ordinal);
                stableState = StateOff;
            }

            public ProcessActivityReading Capture(int currentSessionId)
            {
                Dictionary<string, double> currentCpuMilliseconds = new Dictionary<string, double>(StringComparer.Ordinal);
                int presenceMask = 0;
                bool reliable = true;
                long currentTimestamp = Stopwatch.GetTimestamp();

                for (int nameIndex = 0; nameIndex < processNames.Length; nameIndex++)
                {
                    Process[] processes = null;

                    try
                    {
                        processes = Process.GetProcessesByName(processNames[nameIndex]);

                        for (int processIndex = 0; processIndex < processes.Length; processIndex++)
                        {
                            Process process = processes[processIndex];

                            try
                            {
                                if (process.SessionId != currentSessionId)
                                {
                                    continue;
                                }

                                presenceMask |= 1 << nameIndex;
                                string identity = nameIndex.ToString() + ":" + process.Id.ToString() + ":" + process.StartTime.ToUniversalTime().Ticks.ToString();
                                currentCpuMilliseconds[identity] = process.TotalProcessorTime.TotalMilliseconds;
                            }
                            catch (InvalidOperationException)
                            {
                            }
                            catch (ArgumentException)
                            {
                            }
                            catch (Win32Exception)
                            {
                                reliable = false;
                            }
                            catch (NotSupportedException)
                            {
                                reliable = false;
                            }
                        }
                    }
                    catch (Exception)
                    {
                        reliable = false;
                    }
                    finally
                    {
                        if (processes != null)
                        {
                            for (int processIndex = 0; processIndex < processes.Length; processIndex++)
                            {
                                processes[processIndex].Dispose();
                            }
                        }
                    }
                }

                if (presenceMask == 0)
                {
                    Reset(currentCpuMilliseconds, currentTimestamp, StateOff);
                    return new ProcessActivityReading(StateOff, 0, reliable);
                }

                if (!reliable || currentCpuMilliseconds.Count == 0)
                {
                    Reset(currentCpuMilliseconds, currentTimestamp, StateUnknown);
                    return new ProcessActivityReading(StateUnknown, presenceMask, false);
                }

                double elapsedMilliseconds = previousTimestamp == 0
                    ? 0.0
                    : (currentTimestamp - previousTimestamp) * 1000.0 / Stopwatch.Frequency;
                double cpuDeltaMilliseconds = 0.0;
                int matchedProcesses = 0;

                if (elapsedMilliseconds >= MinimumSampleMilliseconds &&
                    elapsedMilliseconds <= MaximumSampleMilliseconds)
                {
                    foreach (KeyValuePair<string, double> current in currentCpuMilliseconds)
                    {
                        double previous;
                        if (previousCpuMilliseconds.TryGetValue(current.Key, out previous) && current.Value >= previous)
                        {
                            cpuDeltaMilliseconds += current.Value - previous;
                            matchedProcesses++;
                        }
                    }
                }

                previousCpuMilliseconds = currentCpuMilliseconds;
                previousTimestamp = currentTimestamp;

                if (elapsedMilliseconds > MaximumSampleMilliseconds)
                {
                    if (stableState != StateBusy && stableState != StateIdle)
                    {
                        stableState = StateIdle;
                    }

                    usageEmaValid = false;
                    busySamples = 0;
                    quietSamples = 0;
                    lastStrongActivityTimestamp = 0;
                    return new ProcessActivityReading(stableState, presenceMask, true);
                }

                if (elapsedMilliseconds < MinimumSampleMilliseconds || matchedProcesses == 0)
                {
                    stableState = StateIdle;
                    usageEmaValid = false;
                    busySamples = 0;
                    quietSamples = 0;
                    lastStrongActivityTimestamp = 0;
                    return new ProcessActivityReading(stableState, presenceMask, true);
                }

                double usageRatio = cpuDeltaMilliseconds / elapsedMilliseconds;
                usageEma = usageEmaValid ? usageEma * (1.0 - EmaAlpha) + usageRatio * EmaAlpha : usageRatio;
                usageEmaValid = true;

                if (usageRatio >= BusyStrongRatio)
                {
                    lastStrongActivityTimestamp = currentTimestamp;
                }

                if (stableState == StateBusy)
                {
                    bool holdBusy = lastStrongActivityTimestamp != 0 &&
                                    ElapsedMilliseconds(lastStrongActivityTimestamp, currentTimestamp) < BusyHoldMilliseconds;

                    if (!holdBusy && usageEma <= BusyExitRatio)
                    {
                        quietSamples++;
                        if (quietSamples >= QuietSamplesRequired)
                        {
                            stableState = StateIdle;
                            quietSamples = 0;
                        }
                    }
                    else
                    {
                        quietSamples = 0;
                    }
                }
                else
                {
                    stableState = StateIdle;

                    if (allowImmediateBusy && usageRatio >= BusyImmediateRatio)
                    {
                        EnterBusy(currentTimestamp);
                    }
                    else if (usageEma >= BusyEnterRatio)
                    {
                        busySamples++;
                        if (busySamples >= BusySamplesRequired)
                        {
                            EnterBusy(currentTimestamp);
                        }
                    }
                    else
                    {
                        busySamples = 0;
                    }
                }

                return new ProcessActivityReading(stableState, presenceMask, true);
            }

            public void SuppressActivity()
            {
                stableState = StateIdle;
                lastStrongActivityTimestamp = 0;
                usageEma = 0.0;
                usageEmaValid = false;
                busySamples = 0;
                quietSamples = 0;
            }

            private void EnterBusy(long currentTimestamp)
            {
                stableState = StateBusy;
                busySamples = 0;
                quietSamples = 0;
                lastStrongActivityTimestamp = currentTimestamp;
            }

            private void Reset(Dictionary<string, double> currentCpuMilliseconds, long currentTimestamp, byte state)
            {
                previousCpuMilliseconds = currentCpuMilliseconds;
                previousTimestamp = currentTimestamp;
                lastStrongActivityTimestamp = 0;
                stableState = state;
                usageEma = 0.0;
                usageEmaValid = false;
                busySamples = 0;
                quietSamples = 0;
            }

            private static double ElapsedMilliseconds(long startTimestamp, long endTimestamp)
            {
                return (endTimestamp - startTimestamp) * 1000.0 / Stopwatch.Frequency;
            }
        }

        private sealed class CodexTranscriptMonitor : IDisposable
        {
            private const int MaximumTrackedFiles = 12;
            private const int MaximumPendingWatcherPaths = 512;
            private const long MaximumInitialBytes = 8L * 1024L * 1024L;
            private static readonly TimeSpan DiscoveryInterval = TimeSpan.FromSeconds(10);
            private static readonly TimeSpan FullDiscoveryInterval = TimeSpan.FromMinutes(3);
            private static readonly TimeSpan WatcherSetupRetryInterval = TimeSpan.FromSeconds(30);
            private readonly object sync = new object();
            private readonly object watcherEventSync = new object();
            private readonly object watcherLifecycleSync = new object();
            private readonly ManualResetEvent stopEvent = new ManualResetEvent(false);
            private readonly Dictionary<string, TranscriptFileState> files =
                new Dictionary<string, TranscriptFileState>(StringComparer.OrdinalIgnoreCase);
            private readonly Dictionary<string, DateTime> pendingChangedPaths =
                new Dictionary<string, DateTime>(StringComparer.OrdinalIgnoreCase);
            private readonly HashSet<string> pendingDeletedPaths =
                new HashSet<string>(StringComparer.OrdinalIgnoreCase);
            private readonly string sessionsRoot;
            private FileSystemWatcher sessionWatcher;
            private Thread worker;
            private CodexTranscriptSnapshot snapshot;
            private DateTime nextDiscoveryUtc;
            private DateTime nextFullDiscoveryUtc;
            private DateTime nextWatcherSetupUtc;
            private bool watcherForceFullDiscovery;
            private bool watcherResetRequested;
            private bool watcherCoverageGap;
            private bool initialDiscoveryCompleted;
            private bool started;
            private bool disposed;

            public CodexTranscriptMonitor()
            {
                string profile = Environment.GetFolderPath(Environment.SpecialFolder.UserProfile);
                sessionsRoot = Path.Combine(profile, ".codex", "sessions");
                snapshot = CodexTranscriptSnapshot.Empty(false);
            }

            public void Start()
            {
                lock (sync)
                {
                    if (disposed || started)
                    {
                        return;
                    }

                    started = true;
                    worker = new Thread(Run);
                    worker.IsBackground = true;
                    worker.Name = "Codex transcript metadata monitor";
                    worker.Start();
                }
            }

            public CodexTranscriptSnapshot GetSnapshot()
            {
                lock (sync)
                {
                    return snapshot;
                }
            }

            private void Run()
            {
                while (!stopEvent.WaitOne(0))
                {
                    try
                    {
                        Scan();
                    }
                    catch (Exception)
                    {
                        Publish(CodexTranscriptSnapshot.Empty(false));
                    }

                    if (stopEvent.WaitOne(1000))
                    {
                        break;
                    }
                }
            }

            private void Scan()
            {
                if (!Directory.Exists(sessionsRoot))
                {
                    ResetWatcher(true);
                    files.Clear();
                    watcherCoverageGap = true;
                    initialDiscoveryCompleted = false;
                    nextDiscoveryUtc = DateTime.MinValue;
                    nextFullDiscoveryUtc = DateTime.MinValue;
                    Publish(CodexTranscriptSnapshot.Empty(false));
                    return;
                }

                DateTime nowUtc = DateTime.UtcNow;
                Dictionary<string, DateTime> changedPaths;
                string[] deletedPaths;
                bool forceFullDiscovery;
                bool resetWatcher;
                DrainWatcherEvents(
                    out changedPaths,
                    out deletedPaths,
                    out forceFullDiscovery,
                    out resetWatcher);

                if (resetWatcher)
                {
                    watcherCoverageGap = true;
                    ResetWatcher(false);
                }

                bool watcherAvailable = EnsureWatcher(nowUtc);
                RemoveDeletedFiles(deletedPaths);

                bool scheduledDiscovery = nextDiscoveryUtc <= nowUtc;
                bool watcherRecoveryDiscovery = watcherAvailable && watcherCoverageGap;
                bool fullDiscovery = forceFullDiscovery ||
                                     watcherRecoveryDiscovery ||
                                     !initialDiscoveryCompleted ||
                                     nextFullDiscoveryUtc <= nowUtc;

                if (scheduledDiscovery || fullDiscovery || changedPaths.Count > 0)
                {
                    DiscoverFiles(fullDiscovery, nowUtc, changedPaths);
                    initialDiscoveryCompleted = true;

                    if (scheduledDiscovery || fullDiscovery)
                    {
                        nextDiscoveryUtc = nowUtc + DiscoveryInterval;
                    }

                    if (fullDiscovery)
                    {
                        nextFullDiscoveryUtc = nowUtc + FullDiscoveryInterval;

                        if (watcherAvailable)
                        {
                            watcherCoverageGap = false;
                        }
                    }
                }

                foreach (TranscriptFileState file in files.Values)
                {
                    try
                    {
                        file.Scan(MaximumInitialBytes);
                    }
                    catch (IOException)
                    {
                        file.ParseHealthy = false;
                    }
                    catch (UnauthorizedAccessException)
                    {
                        file.ParseHealthy = false;
                    }
                }

                Publish(BuildSnapshot(nowUtc, true));
            }

            private void DiscoverFiles(bool fullDiscovery,
                                       DateTime nowUtc,
                                       Dictionary<string, DateTime> changedPaths)
            {
                Dictionary<string, TranscriptFileCandidate> candidateMap =
                    new Dictionary<string, TranscriptFileCandidate>(StringComparer.OrdinalIgnoreCase);
                string root = Path.GetFullPath(sessionsRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;

                foreach (KeyValuePair<string, TranscriptFileState> file in files)
                {
                    AddCandidate(file.Key, root, candidateMap, file.Value.LastWriteUtc);
                }

                foreach (KeyValuePair<string, DateTime> changedPath in changedPaths)
                {
                    if (IsSafeWatcherPath(changedPath.Key, root))
                    {
                        AddCandidate(changedPath.Key, root, candidateMap, changedPath.Value);
                    }
                }

                if (fullDiscovery)
                {
                    CollectFiles(sessionsRoot, root, 0, candidateMap);
                }
                else
                {
                    CollectRecentFiles(nowUtc, root, candidateMap);
                }

                List<TranscriptFileCandidate> candidates = new List<TranscriptFileCandidate>(candidateMap.Values);
                candidates.Sort(
                    delegate(TranscriptFileCandidate left, TranscriptFileCandidate right)
                    {
                        return right.LastWriteUtc.CompareTo(left.LastWriteUtc);
                    });

                HashSet<string> selected = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                int selectedCount = Math.Min(MaximumTrackedFiles, candidates.Count);
                for (int index = 0; index < selectedCount; index++)
                {
                    string path = candidates[index].Path;
                    selected.Add(path);

                    TranscriptFileState existing;
                    if (!files.TryGetValue(path, out existing))
                    {
                        files[path] = new TranscriptFileState(path, candidates[index].LastWriteUtc);
                    }
                    else
                    {
                        if (candidates[index].LastWriteUtc > existing.LastWriteUtc)
                        {
                            existing.LastWriteUtc = candidates[index].LastWriteUtc;
                        }
                    }
                }

                List<string> removed = new List<string>();
                foreach (string path in files.Keys)
                {
                    if (!selected.Contains(path))
                    {
                        removed.Add(path);
                    }
                }

                for (int index = 0; index < removed.Count; index++)
                {
                    files.Remove(removed[index]);
                }
            }

            private void CollectRecentFiles(DateTime nowUtc,
                                            string root,
                                            Dictionary<string, TranscriptFileCandidate> candidates)
            {
                HashSet<string> directories = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
                DateTime localDate = nowUtc.ToLocalTime().Date;
                DateTime utcDate = nowUtc.Date;

                for (int dayOffset = 0; dayOffset < 3; dayOffset++)
                {
                    AddDateDirectory(localDate.AddDays(-dayOffset), directories);
                    AddDateDirectory(utcDate.AddDays(-dayOffset), directories);
                }

                foreach (string directory in directories)
                {
                    if (Directory.Exists(directory))
                    {
                        CollectFiles(directory, root, 0, candidates);
                    }
                }
            }

            private void AddDateDirectory(DateTime date, HashSet<string> directories)
            {
                directories.Add(Path.Combine(
                    sessionsRoot,
                    date.Year.ToString("0000", CultureInfo.InvariantCulture),
                    date.Month.ToString("00", CultureInfo.InvariantCulture),
                    date.Day.ToString("00", CultureInfo.InvariantCulture)));
            }

            private static void CollectFiles(string directory,
                                              string root,
                                              int depth,
                                              Dictionary<string, TranscriptFileCandidate> candidates)
            {
                if (depth > 8)
                {
                    return;
                }

                string[] paths;
                try
                {
                    paths = Directory.GetFiles(directory, "*.jsonl", SearchOption.TopDirectoryOnly);
                }
                catch (Exception)
                {
                    return;
                }

                for (int index = 0; index < paths.Length; index++)
                {
                    try
                    {
                        string fullPath = Path.GetFullPath(paths[index]);
                        if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        {
                            continue;
                        }

                        FileAttributes attributes = File.GetAttributes(fullPath);
                        if ((attributes & FileAttributes.ReparsePoint) != 0)
                        {
                            continue;
                        }

                        AddCandidate(fullPath, root, candidates);
                    }
                    catch (Exception)
                    {
                    }
                }

                string[] directories;
                try
                {
                    directories = Directory.GetDirectories(directory);
                }
                catch (Exception)
                {
                    return;
                }

                for (int index = 0; index < directories.Length; index++)
                {
                    try
                    {
                        string fullPath = Path.GetFullPath(directories[index]);
                        if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        {
                            continue;
                        }

                        FileAttributes attributes = File.GetAttributes(fullPath);
                        if ((attributes & FileAttributes.ReparsePoint) == 0)
                        {
                            CollectFiles(fullPath, root, depth + 1, candidates);
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }

            private static void AddCandidate(string path,
                                              string root,
                                              Dictionary<string, TranscriptFileCandidate> candidates)
            {
                AddCandidate(path, root, candidates, DateTime.MinValue);
            }

            private static void AddCandidate(string path,
                                              string root,
                                              Dictionary<string, TranscriptFileCandidate> candidates,
                                              DateTime minimumLastWriteUtc)
            {
                try
                {
                    string fullPath = Path.GetFullPath(path);
                    if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(fullPath))
                    {
                        return;
                    }

                    FileAttributes attributes = File.GetAttributes(fullPath);
                    if ((attributes & FileAttributes.ReparsePoint) != 0)
                    {
                        return;
                    }

                    DateTime lastWriteUtc = File.GetLastWriteTimeUtc(fullPath);
                    if (minimumLastWriteUtc > lastWriteUtc)
                    {
                        lastWriteUtc = minimumLastWriteUtc;
                    }

                    TranscriptFileCandidate existing;
                    if (!candidates.TryGetValue(fullPath, out existing) || lastWriteUtc > existing.LastWriteUtc)
                    {
                        candidates[fullPath] = new TranscriptFileCandidate(fullPath, lastWriteUtc);
                    }
                }
                catch (Exception)
                {
                }
            }

            private static bool IsSafeWatcherPath(string path, string root)
            {
                try
                {
                    if (string.IsNullOrEmpty(path) ||
                        !string.Equals(Path.GetExtension(path), ".jsonl", StringComparison.OrdinalIgnoreCase))
                    {
                        return false;
                    }

                    string fullPath = Path.GetFullPath(path);
                    if (!fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase) || !File.Exists(fullPath))
                    {
                        return false;
                    }

                    string rootDirectory = root.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar);
                    string directory = Path.GetDirectoryName(fullPath);
                    int depth = 0;

                    while (!string.Equals(directory, rootDirectory, StringComparison.OrdinalIgnoreCase))
                    {
                        if (string.IsNullOrEmpty(directory) ||
                            !directory.StartsWith(root, StringComparison.OrdinalIgnoreCase) ||
                            depth >= 8)
                        {
                            return false;
                        }

                        FileAttributes attributes = File.GetAttributes(directory);
                        if ((attributes & FileAttributes.ReparsePoint) != 0)
                        {
                            return false;
                        }

                        directory = Path.GetDirectoryName(directory);
                        depth++;
                    }

                    return true;
                }
                catch (Exception)
                {
                    return false;
                }
            }

            private bool EnsureWatcher(DateTime nowUtc)
            {
                lock (watcherLifecycleSync)
                {
                    if (sessionWatcher != null)
                    {
                        return true;
                    }

                    if (stopEvent.WaitOne(0) || nowUtc < nextWatcherSetupUtc)
                    {
                        return false;
                    }

                    nextWatcherSetupUtc = nowUtc + WatcherSetupRetryInterval;
                    FileSystemWatcher watcher = null;

                    try
                    {
                        watcher = new FileSystemWatcher(sessionsRoot, "*.jsonl");
                        watcher.IncludeSubdirectories = true;
                        watcher.NotifyFilter = NotifyFilters.FileName |
                                               NotifyFilters.DirectoryName |
                                               NotifyFilters.LastWrite |
                                               NotifyFilters.Size |
                                               NotifyFilters.CreationTime;
                        watcher.InternalBufferSize = 16 * 1024;
                        watcher.Changed += OnTranscriptChanged;
                        watcher.Created += OnTranscriptChanged;
                        watcher.Deleted += OnTranscriptDeleted;
                        watcher.Renamed += OnTranscriptRenamed;
                        watcher.Error += OnWatcherError;
                        sessionWatcher = watcher;
                        watcher.EnableRaisingEvents = true;
                        nextWatcherSetupUtc = DateTime.MaxValue;
                        return true;
                    }
                    catch (Exception)
                    {
                        watcherCoverageGap = true;

                        if (ReferenceEquals(sessionWatcher, watcher))
                        {
                            sessionWatcher = null;
                        }

                        if (watcher != null)
                        {
                            watcher.Dispose();
                        }

                        return false;
                    }
                }
            }

            private void OnTranscriptChanged(object sender, FileSystemEventArgs args)
            {
                QueueChangedPath(args.FullPath, DateTime.UtcNow);
            }

            private void OnTranscriptDeleted(object sender, FileSystemEventArgs args)
            {
                QueueDeletedPath(args.FullPath);
            }

            private void OnTranscriptRenamed(object sender, RenamedEventArgs args)
            {
                lock (watcherEventSync)
                {
                    if (watcherForceFullDiscovery)
                    {
                        return;
                    }

                    QueueDeletedPathLocked(args.OldFullPath);
                    QueueChangedPathLocked(args.FullPath, DateTime.UtcNow);
                }
            }

            private void OnWatcherError(object sender, ErrorEventArgs args)
            {
                lock (watcherEventSync)
                {
                    pendingChangedPaths.Clear();
                    pendingDeletedPaths.Clear();
                    watcherForceFullDiscovery = true;
                    watcherResetRequested = true;
                }
            }

            private void QueueChangedPath(string path, DateTime observedUtc)
            {
                lock (watcherEventSync)
                {
                    if (watcherForceFullDiscovery)
                    {
                        return;
                    }

                    QueueChangedPathLocked(path, observedUtc);
                }
            }

            private void QueueChangedPathLocked(string path, DateTime observedUtc)
            {
                if (!IsWatcherExtension(path))
                {
                    return;
                }

                DateTime existing;
                if (pendingChangedPaths.TryGetValue(path, out existing))
                {
                    if (observedUtc > existing)
                    {
                        pendingChangedPaths[path] = observedUtc;
                    }

                    return;
                }

                if (pendingChangedPaths.Count + pendingDeletedPaths.Count >= MaximumPendingWatcherPaths)
                {
                    pendingChangedPaths.Clear();
                    pendingDeletedPaths.Clear();
                    watcherForceFullDiscovery = true;
                    return;
                }

                pendingDeletedPaths.Remove(path);
                pendingChangedPaths[path] = observedUtc;
            }

            private void QueueDeletedPath(string path)
            {
                lock (watcherEventSync)
                {
                    if (watcherForceFullDiscovery)
                    {
                        return;
                    }

                    QueueDeletedPathLocked(path);
                }
            }

            private void QueueDeletedPathLocked(string path)
            {
                if (!IsWatcherExtension(path))
                {
                    return;
                }

                if (!pendingDeletedPaths.Contains(path) &&
                    pendingChangedPaths.Count + pendingDeletedPaths.Count >= MaximumPendingWatcherPaths)
                {
                    pendingChangedPaths.Clear();
                    pendingDeletedPaths.Clear();
                    watcherForceFullDiscovery = true;
                    return;
                }

                pendingChangedPaths.Remove(path);
                pendingDeletedPaths.Add(path);
            }

            private static bool IsWatcherExtension(string path)
            {
                try
                {
                    return !string.IsNullOrEmpty(path) &&
                           string.Equals(Path.GetExtension(path), ".jsonl", StringComparison.OrdinalIgnoreCase);
                }
                catch (Exception)
                {
                    return false;
                }
            }

            private void DrainWatcherEvents(out Dictionary<string, DateTime> changedPaths,
                                            out string[] deletedPaths,
                                            out bool forceFullDiscovery,
                                            out bool resetWatcher)
            {
                lock (watcherEventSync)
                {
                    changedPaths = new Dictionary<string, DateTime>(pendingChangedPaths, StringComparer.OrdinalIgnoreCase);
                    deletedPaths = new string[pendingDeletedPaths.Count];
                    pendingDeletedPaths.CopyTo(deletedPaths);
                    forceFullDiscovery = watcherForceFullDiscovery;
                    resetWatcher = watcherResetRequested;
                    pendingChangedPaths.Clear();
                    pendingDeletedPaths.Clear();
                    watcherForceFullDiscovery = false;
                    watcherResetRequested = false;
                }
            }

            private void RemoveDeletedFiles(string[] deletedPaths)
            {
                string root = Path.GetFullPath(sessionsRoot).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar) + Path.DirectorySeparatorChar;

                for (int index = 0; index < deletedPaths.Length; index++)
                {
                    try
                    {
                        string fullPath = Path.GetFullPath(deletedPaths[index]);
                        if (fullPath.StartsWith(root, StringComparison.OrdinalIgnoreCase))
                        {
                            files.Remove(fullPath);
                        }
                    }
                    catch (Exception)
                    {
                    }
                }
            }

            private void ResetWatcher(bool clearPendingEvents)
            {
                FileSystemWatcher watcher;

                lock (watcherLifecycleSync)
                {
                    watcher = sessionWatcher;
                    sessionWatcher = null;
                    nextWatcherSetupUtc = DateTime.MinValue;
                }

                if (watcher != null)
                {
                    try
                    {
                        watcher.EnableRaisingEvents = false;
                    }
                    catch (Exception)
                    {
                    }

                    try
                    {
                        watcher.Changed -= OnTranscriptChanged;
                        watcher.Created -= OnTranscriptChanged;
                        watcher.Deleted -= OnTranscriptDeleted;
                        watcher.Renamed -= OnTranscriptRenamed;
                        watcher.Error -= OnWatcherError;
                    }
                    catch (Exception)
                    {
                    }

                    try
                    {
                        watcher.Dispose();
                    }
                    catch (Exception)
                    {
                    }
                }

                if (clearPendingEvents)
                {
                    lock (watcherEventSync)
                    {
                        pendingChangedPaths.Clear();
                        pendingDeletedPaths.Clear();
                        watcherForceFullDiscovery = false;
                        watcherResetRequested = false;
                    }
                }
            }

            private CodexTranscriptSnapshot BuildSnapshot(DateTime nowUtc, bool scanHealthy)
            {
                bool healthy = scanHealthy;
                Dictionary<string, TranscriptFileState> filesBySessionId =
                    new Dictionary<string, TranscriptFileState>(StringComparer.OrdinalIgnoreCase);

                foreach (TranscriptFileState file in files.Values)
                {
                    if (file.HasRelevantParseFailure(nowUtc))
                    {
                        healthy = false;
                    }

                    if (!file.ParseHealthy || string.IsNullOrEmpty(file.SessionId))
                    {
                        continue;
                    }

                    TranscriptFileState existing;
                    if (!filesBySessionId.TryGetValue(file.SessionId, out existing) ||
                        file.LastWriteUtc > existing.LastWriteUtc)
                    {
                        filesBySessionId[file.SessionId] = file;
                    }
                }

                Dictionary<string, List<SessionSummary>> groupedSummaries =
                    new Dictionary<string, List<SessionSummary>>(StringComparer.OrdinalIgnoreCase);
                Dictionary<string, SessionSummary> rootSummaries =
                    new Dictionary<string, SessionSummary>(StringComparer.OrdinalIgnoreCase);

                foreach (TranscriptFileState file in files.Values)
                {
                    if (!file.ParseHealthy)
                    {
                        continue;
                    }

                    SessionSummary summary = file.CreateSummary(nowUtc);
                    if (summary == null)
                    {
                        continue;
                    }

                    string rootId = ResolveRootSessionId(file, filesBySessionId);
                    List<SessionSummary> group;
                    if (!groupedSummaries.TryGetValue(rootId, out group))
                    {
                        group = new List<SessionSummary>();
                        groupedSummaries[rootId] = group;
                    }

                    group.Add(summary);

                    if (string.Equals(file.Identity, rootId, StringComparison.OrdinalIgnoreCase))
                    {
                        SessionSummary existingRoot;
                        if (!rootSummaries.TryGetValue(rootId, out existingRoot) ||
                            summary.LastEventUtc > existingRoot.LastEventUtc)
                        {
                            rootSummaries[rootId] = summary;
                        }
                    }
                }

                List<SessionSummary> summaries = new List<SessionSummary>();
                foreach (KeyValuePair<string, List<SessionSummary>> group in groupedSummaries)
                {
                    SessionSummary rootSummary;
                    rootSummaries.TryGetValue(group.Key, out rootSummary);
                    summaries.Add(MergeSummaries(group.Key, group.Value, rootSummary));
                }

                bool anyBusy = false;
                bool anyWaiting = false;
                bool anyStaleActive = false;
                bool hasReliableBoundary = false;
                bool allReliableInactive = true;

                foreach (SessionSummary summary in summaries)
                {
                    anyBusy = anyBusy || summary.IsBusy;
                    anyWaiting = anyWaiting || summary.IsWaiting;
                    anyStaleActive = anyStaleActive || summary.IsStaleActive;
                    hasReliableBoundary = hasReliableBoundary || summary.HasReliableBoundary;

                    if ((summary.HasReliableBoundary && !summary.IsReliablyInactive) ||
                        (!summary.HasReliableBoundary && summary.IsRecent))
                    {
                        allReliableInactive = false;
                    }
                }

                if (!hasReliableBoundary)
                {
                    allReliableInactive = false;
                }

                summaries.Sort(
                    delegate(SessionSummary left, SessionSummary right)
                    {
                        int priority = right.Priority.CompareTo(left.Priority);
                        return priority != 0 ? priority : right.LastEventUtc.CompareTo(left.LastEventUtc);
                    });

                int outputCount = Math.Min(MaximumTrackedFiles, summaries.Count);
                SessionSummary[] output = new SessionSummary[outputCount];
                for (int index = 0; index < outputCount; index++)
                {
                    output[index] = summaries[index];
                }

                return new CodexTranscriptSnapshot(
                    nowUtc,
                    healthy,
                    anyBusy,
                    anyWaiting,
                    anyStaleActive,
                    hasReliableBoundary,
                    allReliableInactive,
                    output,
                    summaries.Count);
            }

            private static string ResolveRootSessionId(TranscriptFileState file,
                                                       Dictionary<string, TranscriptFileState> filesBySessionId)
            {
                string rootId = file.Identity;
                string parentId = file.ParentThreadId;
                HashSet<string> visited = new HashSet<string>(StringComparer.OrdinalIgnoreCase);

                for (int depth = 0; depth < 16 && !string.IsNullOrEmpty(parentId); depth++)
                {
                    if (!visited.Add(parentId))
                    {
                        break;
                    }

                    rootId = parentId;
                    TranscriptFileState parent;
                    if (!filesBySessionId.TryGetValue(parentId, out parent))
                    {
                        break;
                    }

                    parentId = parent.ParentThreadId;
                }

                return rootId;
            }

            private static SessionSummary MergeSummaries(string rootId,
                                                         List<SessionSummary> members,
                                                         SessionSummary rootSummary)
            {
                SessionSummary dominant = rootSummary ?? members[0];
                DateTime lastEventUtc = DateTime.MinValue;
                bool hasReliableBoundary = false;
                bool allReliablyInactive = true;
                bool isBusy = false;
                bool isWaiting = false;
                bool isStaleActive = false;
                bool isRecent = false;
                int priority = 0;

                for (int index = 0; index < members.Count; index++)
                {
                    SessionSummary member = members[index];
                    if (member.Priority > dominant.Priority ||
                        (member.Priority == dominant.Priority && member.LastEventUtc > dominant.LastEventUtc))
                    {
                        dominant = member;
                    }

                    if (member.LastEventUtc > lastEventUtc)
                    {
                        lastEventUtc = member.LastEventUtc;
                    }

                    hasReliableBoundary = hasReliableBoundary || member.HasReliableBoundary;
                    allReliablyInactive = allReliablyInactive && member.IsReliablyInactive;
                    isBusy = isBusy || member.IsBusy;
                    isWaiting = isWaiting || member.IsWaiting;
                    isStaleActive = isStaleActive || member.IsStaleActive;
                    isRecent = isRecent || member.IsRecent;
                    priority = Math.Max(priority, member.Priority);
                }

                string label = rootSummary != null
                    ? rootSummary.Label
                    : TranscriptFileState.BuildFallbackLabel(rootId);

                return new SessionSummary(
                    label,
                    dominant.State,
                    lastEventUtc,
                    hasReliableBoundary,
                    hasReliableBoundary && allReliablyInactive,
                    isBusy,
                    isWaiting,
                    isStaleActive,
                    isRecent,
                    priority);
            }

            private void Publish(CodexTranscriptSnapshot value)
            {
                lock (sync)
                {
                    snapshot = value;
                }
            }

            public void Dispose()
            {
                Thread thread;

                lock (sync)
                {
                    if (disposed)
                    {
                        return;
                    }

                    disposed = true;
                    stopEvent.Set();
                    thread = worker;
                }

                ResetWatcher(true);

                if (thread != null && thread != Thread.CurrentThread)
                {
                    thread.Join(2000);
                }
            }
        }

        private sealed class TranscriptFileCandidate
        {
            public string Path { get; private set; }
            public DateTime LastWriteUtc { get; private set; }

            public TranscriptFileCandidate(string path, DateTime lastWriteUtc)
            {
                Path = path;
                LastWriteUtc = lastWriteUtc;
            }
        }

        private sealed class TranscriptFileState
        {
            private static readonly TimeSpan FreshActiveTime = TimeSpan.FromMinutes(5);
            private static readonly TimeSpan RecentTime = TimeSpan.FromMinutes(15);
            private static readonly TimeSpan PendingTime = TimeSpan.FromSeconds(15);
            private static readonly TimeSpan DoneDisplayTime = TimeSpan.FromMinutes(5);
            private static readonly TimeSpan FailedDisplayTime = TimeSpan.FromMinutes(1);
            private long offset;
            private long fileCreationTicks;
            private long fileLastWriteTicks;
            private long observedLength = -1;
            private uint fileIdentityFingerprint;
            private bool historyComplete;
            private bool fileIdentityFingerprintValid;
            private bool taskActive;
            private bool waitingForStart;
            private bool hasReliableBoundary;
            private byte terminalState;
            private DateTime lastEventUtc;
            private DateTime lastEvidenceUtc;
            private string sessionId;
            private string parentThreadId;
            private string cwd;
            private string agentNickname;
            private bool parseRecoveryPending;

            public string Path { get; private set; }
            public DateTime LastWriteUtc { get; set; }
            public bool ParseHealthy { get; set; }
            public string SessionId { get { return sessionId; } }
            public string ParentThreadId { get { return parentThreadId; } }
            public string Identity { get { return !string.IsNullOrEmpty(sessionId) ? sessionId : Path; } }

            public TranscriptFileState(string path, DateTime lastWriteUtc)
            {
                Path = path;
                LastWriteUtc = lastWriteUtc;
                historyComplete = true;
                terminalState = DetailOff;
                ParseHealthy = true;
            }

            public void Scan(long maximumInitialBytes)
            {
                if (!parseRecoveryPending)
                {
                    ParseHealthy = true;
                }

                using (FileStream stream = new FileStream(Path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite | FileShare.Delete))
                {
                    long currentCreationTicks = File.GetCreationTimeUtc(Path).Ticks;
                    long currentLastWriteTicks = File.GetLastWriteTimeUtc(Path).Ticks;
                    long currentLength = stream.Length;
                    uint currentFingerprint;
                    bool currentFingerprintValid = TryReadFileIdentityFingerprint(stream, out currentFingerprint);
                    bool replaced = currentLength < offset ||
                                    (fileCreationTicks != 0 && currentCreationTicks != fileCreationTicks) ||
                                    (observedLength >= 0 &&
                                     currentLength == observedLength &&
                                     fileLastWriteTicks != 0 &&
                                     currentLastWriteTicks != fileLastWriteTicks) ||
                                    (fileIdentityFingerprintValid &&
                                     currentFingerprintValid &&
                                     currentFingerprint != fileIdentityFingerprint);

                    if (replaced)
                    {
                        ResetForReplacement();
                    }

                    fileCreationTicks = currentCreationTicks;
                    fileLastWriteTicks = currentLastWriteTicks;
                    observedLength = currentLength;
                    if (currentFingerprintValid)
                    {
                        fileIdentityFingerprint = currentFingerprint;
                        fileIdentityFingerprintValid = true;
                    }

                    if (offset == 0 && stream.Length > maximumInitialBytes)
                    {
                        TranscriptMetadataEvent sessionMetadata;
                        SelectiveJsonMetadataReader metadataReader = new SelectiveJsonMetadataReader(stream);
                        try
                        {
                            if (metadataReader.TryReadNext(out sessionMetadata) &&
                                string.Equals(sessionMetadata.RootType, "session_meta", StringComparison.Ordinal))
                            {
                                ApplyParsed(sessionMetadata);
                            }
                        }
                        catch (FormatException)
                        {
                            MarkParseFailure();
                        }
                        catch (EndOfStreamException)
                        {
                        }

                        stream.Position = stream.Length - maximumInitialBytes;
                        SelectiveJsonMetadataReader initialReader = new SelectiveJsonMetadataReader(stream);
                        initialReader.SkipToLineEnd();
                        offset = initialReader.Position;
                        historyComplete = false;
                    }
                    else
                    {
                        stream.Position = offset;
                    }

                    SelectiveJsonMetadataReader reader = new SelectiveJsonMetadataReader(stream);
                    while (true)
                    {
                        long recordStart = reader.Position;
                        TranscriptMetadataEvent metadata;

                        try
                        {
                            if (!reader.TryReadNext(out metadata))
                            {
                                offset = reader.Position;
                                break;
                            }

                            ApplyParsed(metadata);
                            offset = reader.Position;
                        }
                        catch (EndOfStreamException)
                        {
                            offset = recordStart;
                            break;
                        }
                        catch (FormatException)
                        {
                            MarkParseFailure();
                            reader.SkipToLineEnd();
                            offset = reader.Position;
                        }
                    }
                }
            }

            private void ApplyParsed(TranscriptMetadataEvent metadata)
            {
                bool recovering = parseRecoveryPending;
                ParseHealthy = true;
                bool reliableBoundary = Apply(metadata);

                if (!ParseHealthy)
                {
                    parseRecoveryPending = true;
                }
                else if (recovering && !reliableBoundary)
                {
                    ParseHealthy = false;
                    parseRecoveryPending = true;
                }
                else
                {
                    parseRecoveryPending = false;
                }
            }

            private void MarkParseFailure()
            {
                ParseHealthy = false;
                parseRecoveryPending = true;
            }

            private bool Apply(TranscriptMetadataEvent metadata)
            {
                DateTime eventUtc;
                bool reliableBoundary = false;
                bool timestampValid = DateTime.TryParse(
                    metadata.Timestamp,
                    CultureInfo.InvariantCulture,
                    DateTimeStyles.AssumeUniversal | DateTimeStyles.AdjustToUniversal,
                    out eventUtc);

                if (!timestampValid || eventUtc > DateTime.UtcNow + TimeSpan.FromMinutes(2))
                {
                    ParseHealthy = false;
                    eventUtc = LastWriteUtc;
                }

                if (string.Equals(metadata.RootType, "session_meta", StringComparison.Ordinal))
                {
                    sessionId = metadata.Id;
                    parentThreadId = metadata.ParentThreadId;
                    cwd = metadata.Cwd;
                    agentNickname = metadata.AgentNickname;
                    return false;
                }

                lastEventUtc = eventUtc;

                if (string.Equals(metadata.RootType, "event_msg", StringComparison.Ordinal))
                {
                    if (string.Equals(metadata.PayloadType, "task_started", StringComparison.Ordinal))
                    {
                        taskActive = true;
                        waitingForStart = false;
                        hasReliableBoundary = true;
                        terminalState = DetailOff;
                        lastEvidenceUtc = eventUtc;
                        reliableBoundary = true;
                    }
                    else if (string.Equals(metadata.PayloadType, "task_complete", StringComparison.Ordinal))
                    {
                        taskActive = false;
                        waitingForStart = false;
                        hasReliableBoundary = true;
                        terminalState = DetailDone;
                        lastEvidenceUtc = eventUtc;
                        reliableBoundary = true;
                    }
                    else if (string.Equals(metadata.PayloadType, "turn_aborted", StringComparison.Ordinal))
                    {
                        taskActive = false;
                        waitingForStart = false;
                        hasReliableBoundary = true;
                        terminalState = DetailFailed;
                        lastEvidenceUtc = eventUtc;
                        reliableBoundary = true;
                    }
                    else if (string.Equals(metadata.PayloadType, "user_message", StringComparison.Ordinal))
                    {
                        if (!taskActive)
                        {
                            waitingForStart = true;
                            terminalState = DetailOff;
                        }

                        lastEvidenceUtc = eventUtc;
                    }
                    else if (string.Equals(metadata.PayloadType, "agent_reasoning", StringComparison.Ordinal))
                    {
                        taskActive = true;
                        waitingForStart = false;
                        hasReliableBoundary = true;
                        terminalState = DetailOff;
                        lastEvidenceUtc = eventUtc;
                        reliableBoundary = true;
                    }
                }
                else if (string.Equals(metadata.RootType, "response_item", StringComparison.Ordinal) &&
                         IsToolCallType(metadata.PayloadType))
                {
                    taskActive = true;
                    waitingForStart = false;
                    hasReliableBoundary = true;
                    terminalState = DetailOff;
                    lastEvidenceUtc = eventUtc;
                    reliableBoundary = true;
                }

                return reliableBoundary;
            }

            public bool HasRelevantParseFailure(DateTime nowUtc)
            {
                if (ParseHealthy)
                {
                    return false;
                }
                DateTime referenceUtc = lastEvidenceUtc;
                if (LastWriteUtc > referenceUtc)
                {
                    referenceUtc = LastWriteUtc;
                }
                if (referenceUtc == DateTime.MinValue)
                {
                    return false;
                }

                TimeSpan age = nowUtc - referenceUtc;
                return age < TimeSpan.Zero || age <= RecentTime;
            }

            private static bool TryReadFileIdentityFingerprint(FileStream stream, out uint fingerprint)
            {
                const int maximumIdentityBytes = 4096;
                long originalPosition = stream.Position;
                uint hash = 2166136261U;
                int count = 0;
                bool complete = false;

                stream.Position = 0;
                while (count < maximumIdentityBytes)
                {
                    int value = stream.ReadByte();
                    if (value < 0)
                    {
                        break;
                    }

                    hash ^= (byte)value;
                    hash *= 16777619U;
                    count++;
                    if (value == '\n')
                    {
                        complete = true;
                        break;
                    }
                }

                stream.Position = originalPosition;
                fingerprint = hash;
                return complete || count == maximumIdentityBytes;
            }

            private static bool IsToolCallType(string payloadType)
            {
                return !string.IsNullOrEmpty(payloadType) &&
                       (string.Equals(payloadType, "function_call", StringComparison.Ordinal) ||
                        string.Equals(payloadType, "custom_tool_call", StringComparison.Ordinal) ||
                        payloadType.EndsWith("_call", StringComparison.Ordinal));
            }

            public SessionSummary CreateSummary(DateTime nowUtc)
            {
                if (lastEventUtc == DateTime.MinValue && string.IsNullOrEmpty(cwd) && string.IsNullOrEmpty(agentNickname))
                {
                    return null;
                }

                DateTime evidenceUtc = lastEvidenceUtc != DateTime.MinValue ? lastEvidenceUtc : lastEventUtc;
                TimeSpan age = evidenceUtc == DateTime.MinValue ? TimeSpan.MaxValue : nowUtc - evidenceUtc;
                if (age < TimeSpan.Zero)
                {
                    age = TimeSpan.Zero;
                }

                byte state;
                bool isBusy = false;
                bool isWaiting = false;
                bool isStaleActive = false;
                bool reliablyInactive = false;

                if (taskActive)
                {
                    if (age <= FreshActiveTime)
                    {
                        state = DetailBusy;
                        isBusy = true;
                    }
                    else
                    {
                        state = DetailStale;
                        isStaleActive = true;
                    }
                }
                else if (waitingForStart && age <= PendingTime)
                {
                    state = DetailWait;
                    isWaiting = true;
                }
                else if (terminalState == DetailDone || terminalState == DetailFailed)
                {
                    TimeSpan displayTime = terminalState == DetailDone ? DoneDisplayTime : FailedDisplayTime;
                    state = age > displayTime ? DetailStale : terminalState;
                    reliablyInactive = true;
                }
                else if (!hasReliableBoundary && !historyComplete)
                {
                    state = DetailStale;
                }
                else if (age > RecentTime)
                {
                    state = DetailStale;
                }
                else
                {
                    state = DetailIdle;
                }

                string label = BuildLabel();
                int priority = isBusy ? 4 : isWaiting ? 3 : isStaleActive ? 2 : 1;
                return new SessionSummary(
                    label,
                    state,
                    lastEventUtc == DateTime.MinValue ? LastWriteUtc : lastEventUtc,
                    hasReliableBoundary,
                    reliablyInactive,
                    isBusy,
                    isWaiting,
                    isStaleActive,
                    age <= RecentTime,
                    priority);
            }

            private string BuildLabel()
            {
                if (!string.IsNullOrEmpty(agentNickname))
                {
                    return NormalizeAscii(agentNickname, 10, "CODEX");
                }

                string value = null;

                if (!string.IsNullOrEmpty(cwd))
                {
                    try
                    {
                        value = System.IO.Path.GetFileName(
                            cwd.TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar));
                    }
                    catch (Exception)
                    {
                        value = null;
                    }
                }

                if (string.IsNullOrEmpty(value))
                {
                    value = "CODEX";
                }

                string baseLabel = NormalizeAscii(value, 7, "CODEX");
                string identity = !string.IsNullOrEmpty(sessionId) ? sessionId : Path;
                return baseLabel + "-" + StableHashByte(identity).ToString("X2", CultureInfo.InvariantCulture);
            }

            public static string BuildFallbackLabel(string identity)
            {
                return "CODEX-" + StableHashByte(identity).ToString("X2", CultureInfo.InvariantCulture);
            }

            private static string NormalizeAscii(string value, int maximumLength, string fallback)
            {
                StringBuilder label = new StringBuilder(maximumLength);
                for (int index = 0; index < value.Length && label.Length < maximumLength; index++)
                {
                    char character = value[index];
                    if (character >= 0x20 && character <= 0x7E)
                    {
                        label.Append(character);
                    }
                }

                string result = label.ToString().Trim();
                return result.Length == 0 ? fallback : result;
            }

            private static byte StableHashByte(string value)
            {
                uint hash = 2166136261U;

                for (int index = 0; index < value.Length; index++)
                {
                    hash ^= value[index];
                    hash *= 16777619U;
                }

                return (byte)(hash ^ (hash >> 8) ^ (hash >> 16) ^ (hash >> 24));
            }

            private void ResetForReplacement()
            {
                offset = 0;
                fileCreationTicks = 0;
                fileLastWriteTicks = 0;
                observedLength = -1;
                fileIdentityFingerprint = 0;
                historyComplete = true;
                fileIdentityFingerprintValid = false;
                taskActive = false;
                waitingForStart = false;
                hasReliableBoundary = false;
                terminalState = DetailOff;
                lastEventUtc = DateTime.MinValue;
                lastEvidenceUtc = DateTime.MinValue;
                sessionId = null;
                parentThreadId = null;
                cwd = null;
                agentNickname = null;
                parseRecoveryPending = false;
                ParseHealthy = true;
            }
        }

        private sealed class SessionSummary
        {
            public string Label { get; private set; }
            public byte State { get; private set; }
            public DateTime LastEventUtc { get; private set; }
            public bool HasReliableBoundary { get; private set; }
            public bool IsReliablyInactive { get; private set; }
            public bool IsBusy { get; private set; }
            public bool IsWaiting { get; private set; }
            public bool IsStaleActive { get; private set; }
            public bool IsRecent { get; private set; }
            public int Priority { get; private set; }

            public SessionSummary(string label,
                                  byte state,
                                  DateTime lastEventUtc,
                                  bool hasReliableBoundary,
                                  bool isReliablyInactive,
                                  bool isBusy,
                                  bool isWaiting,
                                  bool isStaleActive,
                                  bool isRecent,
                                  int priority)
            {
                Label = label;
                State = state;
                LastEventUtc = lastEventUtc;
                HasReliableBoundary = hasReliableBoundary;
                IsReliablyInactive = isReliablyInactive;
                IsBusy = isBusy;
                IsWaiting = isWaiting;
                IsStaleActive = isStaleActive;
                IsRecent = isRecent;
                Priority = priority;
            }
        }

        private sealed class CodexTranscriptSnapshot
        {
            public DateTime CreatedUtc { get; private set; }
            public bool IsHealthy { get; private set; }
            public bool AnyBusy { get; private set; }
            public bool AnyWaiting { get; private set; }
            public bool AnyStaleActive { get; private set; }
            public bool HasReliableBoundary { get; private set; }
            public bool AllReliableSessionsInactive { get; private set; }
            public SessionSummary[] Summaries { get; private set; }
            public int TotalSessions { get; private set; }

            public CodexTranscriptSnapshot(DateTime createdUtc,
                                           bool isHealthy,
                                           bool anyBusy,
                                           bool anyWaiting,
                                           bool anyStaleActive,
                                           bool hasReliableBoundary,
                                           bool allReliableSessionsInactive,
                                           SessionSummary[] summaries,
                                           int totalSessions)
            {
                CreatedUtc = createdUtc;
                IsHealthy = isHealthy;
                AnyBusy = anyBusy;
                AnyWaiting = anyWaiting;
                AnyStaleActive = anyStaleActive;
                HasReliableBoundary = hasReliableBoundary;
                AllReliableSessionsInactive = allReliableSessionsInactive;
                Summaries = summaries;
                TotalSessions = totalSessions;
            }

            public static CodexTranscriptSnapshot Empty(bool healthy)
            {
                return new CodexTranscriptSnapshot(
                    DateTime.UtcNow,
                    healthy,
                    false,
                    false,
                    false,
                    false,
                    false,
                    new SessionSummary[0],
                    0);
            }
        }

        private sealed class TranscriptMetadataEvent
        {
            public string Timestamp;
            public string RootType;
            public string PayloadType;
            public string Id;
            public string Cwd;
            public string AgentNickname;
            public string ParentThreadId;
        }

        private sealed class SelectiveJsonMetadataReader
        {
            private const int MaximumDepth = 32;
            private readonly Stream stream;
            private int pushedByte = -1;

            public SelectiveJsonMetadataReader(Stream stream)
            {
                this.stream = stream;
            }

            public long Position
            {
                get { return stream.Position - (pushedByte >= 0 ? 1 : 0); }
            }

            public bool TryReadNext(out TranscriptMetadataEvent metadata)
            {
                metadata = null;
                int token = ReadNonWhitespace();
                if (token < 0)
                {
                    return false;
                }
                if (token != '{')
                {
                    throw new FormatException("Invalid JSONL record.");
                }

                metadata = new TranscriptMetadataEvent();
                ReadRootObject(metadata);
                return true;
            }

            public void SkipToLineEnd()
            {
                int value;
                while ((value = ReadByte()) >= 0)
                {
                    if (value == '\n')
                    {
                        break;
                    }
                }
            }

            private void ReadRootObject(TranscriptMetadataEvent metadata)
            {
                bool first = true;

                while (true)
                {
                    int token = ReadRequiredNonWhitespace();
                    if (token == '}')
                    {
                        return;
                    }
                    if (!first)
                    {
                        if (token != ',')
                        {
                            throw new FormatException("Invalid JSON object separator.");
                        }
                        token = ReadRequiredNonWhitespace();
                    }
                    if (token != '"')
                    {
                        throw new FormatException("Invalid JSON property.");
                    }

                    string key = ReadCapturedString(64);
                    Expect(':');

                    if (string.Equals(key, "timestamp", StringComparison.Ordinal))
                    {
                        metadata.Timestamp = ReadStringValue(64);
                    }
                    else if (string.Equals(key, "type", StringComparison.Ordinal))
                    {
                        metadata.RootType = ReadStringValue(64);
                    }
                    else if (string.Equals(key, "payload", StringComparison.Ordinal))
                    {
                        ReadPayload(metadata);
                    }
                    else
                    {
                        SkipValue(0);
                    }

                    first = false;
                }
            }

            private void ReadPayload(TranscriptMetadataEvent metadata)
            {
                int token = ReadRequiredNonWhitespace();
                if (token != '{')
                {
                    Unread(token);
                    SkipValue(0);
                    return;
                }

                bool sessionMetadata = string.Equals(metadata.RootType, "session_meta", StringComparison.Ordinal);
                bool first = true;
                while (true)
                {
                    token = ReadRequiredNonWhitespace();
                    if (token == '}')
                    {
                        return;
                    }
                    if (!first)
                    {
                        if (token != ',')
                        {
                            throw new FormatException("Invalid JSON payload separator.");
                        }
                        token = ReadRequiredNonWhitespace();
                    }
                    if (token != '"')
                    {
                        throw new FormatException("Invalid JSON payload property.");
                    }

                    string key = ReadCapturedString(64);
                    Expect(':');

                    if (string.Equals(key, "type", StringComparison.Ordinal))
                    {
                        metadata.PayloadType = ReadStringValue(64);
                    }
                    else if (sessionMetadata && string.Equals(key, "id", StringComparison.Ordinal))
                    {
                        metadata.Id = ReadStringValue(128);
                    }
                    else if (sessionMetadata && string.Equals(key, "cwd", StringComparison.Ordinal))
                    {
                        metadata.Cwd = ReadStringValue(1024);
                    }
                    else if (sessionMetadata && string.Equals(key, "agent_nickname", StringComparison.Ordinal))
                    {
                        metadata.AgentNickname = ReadStringValue(128);
                    }
                    else if (sessionMetadata && string.Equals(key, "parent_thread_id", StringComparison.Ordinal))
                    {
                        metadata.ParentThreadId = ReadStringValue(128);
                    }
                    else
                    {
                        SkipValue(0);
                    }

                    first = false;
                }
            }

            private string ReadStringValue(int maximumLength)
            {
                int token = ReadRequiredNonWhitespace();
                if (token != '"')
                {
                    Unread(token);
                    SkipValue(0);
                    return null;
                }

                return ReadCapturedString(maximumLength);
            }

            private string ReadCapturedString(int maximumLength)
            {
                StringBuilder value = new StringBuilder(Math.Min(maximumLength, 128));

                while (true)
                {
                    int current = ReadRequiredByte();
                    if (current == '"')
                    {
                        return value.ToString();
                    }
                    if (current == '\\')
                    {
                        int escaped = ReadRequiredByte();
                        if (escaped == 'u')
                        {
                            int codePoint = ReadHex4();
                            AppendCaptured(value, codePoint <= 0x7F ? (char)codePoint : '?', maximumLength);
                        }
                        else
                        {
                            char decoded;
                            switch (escaped)
                            {
                                case 'b': decoded = '\b'; break;
                                case 'f': decoded = '\f'; break;
                                case 'n': decoded = '\n'; break;
                                case 'r': decoded = '\r'; break;
                                case 't': decoded = '\t'; break;
                                default: decoded = (char)escaped; break;
                            }

                            AppendCaptured(value, decoded, maximumLength);
                        }
                    }
                    else if (current < 0x80)
                    {
                        AppendCaptured(value, (char)current, maximumLength);
                    }
                    else
                    {
                        SkipUtf8Continuation(current);
                        AppendCaptured(value, '?', maximumLength);
                    }
                }
            }

            private static void AppendCaptured(StringBuilder value, char character, int maximumLength)
            {
                if (value.Length < maximumLength)
                {
                    value.Append(character);
                }
            }

            private void SkipValue(int depth)
            {
                if (depth > MaximumDepth)
                {
                    throw new FormatException("JSON nesting is too deep.");
                }

                int token = ReadNonWhitespace();
                if (token == '"')
                {
                    SkipString();
                }
                else if (token == '{')
                {
                    SkipObject(depth + 1);
                }
                else if (token == '[')
                {
                    SkipArray(depth + 1);
                }
                else if (token < 0)
                {
                    throw new EndOfStreamException();
                }
                else
                {
                    while (true)
                    {
                        token = ReadByte();
                        if (token < 0)
                        {
                            throw new EndOfStreamException();
                        }
                        if (token == ',' || token == '}' || token == ']')
                        {
                            Unread(token);
                            return;
                        }
                    }
                }
            }

            private void SkipObject(int depth)
            {
                bool first = true;
                while (true)
                {
                    int token = ReadRequiredNonWhitespace();
                    if (token == '}')
                    {
                        return;
                    }
                    if (!first)
                    {
                        if (token != ',')
                        {
                            throw new FormatException("Invalid nested JSON object.");
                        }
                        token = ReadRequiredNonWhitespace();
                    }
                    if (token != '"')
                    {
                        throw new FormatException("Invalid nested JSON property.");
                    }

                    SkipString();
                    Expect(':');
                    SkipValue(depth);
                    first = false;
                }
            }

            private void SkipArray(int depth)
            {
                bool first = true;
                while (true)
                {
                    int token = ReadRequiredNonWhitespace();
                    if (token == ']')
                    {
                        return;
                    }
                    if (!first)
                    {
                        if (token != ',')
                        {
                            throw new FormatException("Invalid JSON array.");
                        }
                    }
                    else
                    {
                        Unread(token);
                    }

                    SkipValue(depth);
                    first = false;
                }
            }

            private void SkipString()
            {
                while (true)
                {
                    int current = ReadRequiredByte();
                    if (current == '"')
                    {
                        return;
                    }
                    if (current == '\\')
                    {
                        int escaped = ReadRequiredByte();
                        if (escaped == 'u')
                        {
                            ReadRequiredByte();
                            ReadRequiredByte();
                            ReadRequiredByte();
                            ReadRequiredByte();
                        }
                    }
                }
            }

            private void SkipUtf8Continuation(int leadingByte)
            {
                int remaining = (leadingByte & 0xE0) == 0xC0 ? 1 :
                                (leadingByte & 0xF0) == 0xE0 ? 2 :
                                (leadingByte & 0xF8) == 0xF0 ? 3 : 0;

                for (int index = 0; index < remaining; index++)
                {
                    int continuation = ReadRequiredByte();
                    if ((continuation & 0xC0) != 0x80)
                    {
                        throw new FormatException("Invalid UTF-8 sequence.");
                    }
                }
            }

            private int ReadHex4()
            {
                int value = 0;
                for (int index = 0; index < 4; index++)
                {
                    int digit = ReadRequiredByte();
                    value <<= 4;
                    if (digit >= '0' && digit <= '9')
                    {
                        value |= digit - '0';
                    }
                    else if (digit >= 'a' && digit <= 'f')
                    {
                        value |= digit - 'a' + 10;
                    }
                    else if (digit >= 'A' && digit <= 'F')
                    {
                        value |= digit - 'A' + 10;
                    }
                    else
                    {
                        throw new FormatException("Invalid JSON escape.");
                    }
                }

                return value;
            }

            private void Expect(int expected)
            {
                int token = ReadRequiredNonWhitespace();
                if (token != expected)
                {
                    throw new FormatException("Invalid JSON token.");
                }
            }

            private int ReadNonWhitespace()
            {
                int value;
                do
                {
                    value = ReadByte();
                }
                while (value == ' ' || value == '\t' || value == '\r' || value == '\n');

                return value;
            }

            private int ReadRequiredNonWhitespace()
            {
                int value = ReadNonWhitespace();
                if (value < 0)
                {
                    throw new EndOfStreamException();
                }

                return value;
            }

            private int ReadRequiredByte()
            {
                int value = ReadByte();
                if (value < 0)
                {
                    throw new EndOfStreamException();
                }

                return value;
            }

            private int ReadByte()
            {
                if (pushedByte >= 0)
                {
                    int value = pushedByte;
                    pushedByte = -1;
                    return value;
                }

                return stream.ReadByte();
            }

            private void Unread(int value)
            {
                if (value < 0 || pushedByte >= 0)
                {
                    throw new FormatException("Invalid JSON reader state.");
                }

                pushedByte = value;
            }
        }

        private sealed class PairedDeviceCatalog : IDisposable
        {
            private readonly object sync = new object();
            private readonly Dictionary<string, DeviceInformation> devices = new Dictionary<string, DeviceInformation>(StringComparer.OrdinalIgnoreCase);
            private readonly DeviceWatcher watcher;
            private readonly TaskCompletionSource<bool> enumerationCompleted;
            private readonly TypedEventHandler<DeviceWatcher, DeviceInformation> addedHandler;
            private readonly TypedEventHandler<DeviceWatcher, DeviceInformationUpdate> updatedHandler;
            private readonly TypedEventHandler<DeviceWatcher, DeviceInformationUpdate> removedHandler;
            private readonly TypedEventHandler<DeviceWatcher, object> enumerationCompletedHandler;
            private readonly TypedEventHandler<DeviceWatcher, object> stoppedHandler;
            private bool started;
            private bool disposed;

            public PairedDeviceCatalog()
            {
                string selector = BluetoothLEDevice.GetDeviceSelectorFromPairingState(true);
                string[] properties = { IsPresentProperty, IsConnectableProperty };
                watcher = DeviceInformation.CreateWatcher(selector, properties, DeviceInformationKind.AssociationEndpoint);
                enumerationCompleted = new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
                addedHandler = OnAdded;
                updatedHandler = OnUpdated;
                removedHandler = OnRemoved;
                enumerationCompletedHandler = OnEnumerationCompleted;
                stoppedHandler = OnStopped;
            }

            public async Task StartAsync(CancellationToken cancellationToken)
            {
                if (disposed)
                {
                    throw new ObjectDisposedException("PairedDeviceCatalog");
                }
                if (started)
                {
                    throw new InvalidOperationException("The paired device watcher is already running.");
                }

                started = true;
                watcher.Added += addedHandler;
                watcher.Updated += updatedHandler;
                watcher.Removed += removedHandler;
                watcher.EnumerationCompleted += enumerationCompletedHandler;
                watcher.Stopped += stoppedHandler;

                try
                {
                    watcher.Start();
                }
                catch (Exception ex)
                {
                    WriteLog("paired BLE device enumeration could not start; using advertisement fallback: " + FormatException(ex));
                    return;
                }

                Task timeoutTask = Task.Delay(DiscoveryTimeout, cancellationToken);
                Task completedTask = await Task.WhenAny(enumerationCompleted.Task, timeoutTask).ConfigureAwait(false);

                if (completedTask != enumerationCompleted.Task)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    WriteLog("paired BLE device enumeration is still running; continuing with the current device list");
                    return;
                }

                bool completedNormally = await enumerationCompleted.Task.ConfigureAwait(false);
                if (!completedNormally)
                {
                    WriteLog("paired BLE device enumeration stopped; using advertisement fallback");
                }
            }

            public List<PairedDeviceCandidate> GetScreenCandidates(string preferredDeviceId)
            {
                List<PairedDeviceCandidate> candidates = new List<PairedDeviceCandidate>();

                lock (sync)
                {
                    foreach (DeviceInformation information in devices.Values)
                    {
                        if (!information.Pairing.IsPaired)
                        {
                            continue;
                        }

                        bool preferred = !string.IsNullOrEmpty(preferredDeviceId) &&
                                         string.Equals(information.Id, preferredDeviceId, StringComparison.OrdinalIgnoreCase);
                        bool matchingName = !string.IsNullOrEmpty(information.Name) &&
                                            information.Name.StartsWith(DeviceName, StringComparison.OrdinalIgnoreCase);

                        if (!preferred && !matchingName)
                        {
                            continue;
                        }

                        candidates.Add(new PairedDeviceCandidate(
                            information.Id,
                            information.Name,
                            GetBooleanProperty(information, IsPresentProperty),
                            GetBooleanProperty(information, IsConnectableProperty)));
                    }
                }

                candidates.Sort(
                    delegate(PairedDeviceCandidate left, PairedDeviceCandidate right)
                    {
                        int leftRank = left.ConnectionRank;
                        int rightRank = right.ConnectionRank;
                        if (leftRank != rightRank)
                        {
                            return leftRank.CompareTo(rightRank);
                        }

                        return string.Compare(left.Name, right.Name, StringComparison.OrdinalIgnoreCase);
                    });

                return candidates;
            }

            private static bool? GetBooleanProperty(DeviceInformation information, string propertyName)
            {
                object value;
                if (!information.Properties.TryGetValue(propertyName, out value) || !(value is bool))
                {
                    return null;
                }

                return (bool)value;
            }

            private void OnAdded(DeviceWatcher sender, DeviceInformation information)
            {
                lock (sync)
                {
                    if (!disposed)
                    {
                        devices[information.Id] = information;
                    }
                }
            }

            private void OnUpdated(DeviceWatcher sender, DeviceInformationUpdate update)
            {
                lock (sync)
                {
                    DeviceInformation information;
                    if (!disposed && devices.TryGetValue(update.Id, out information))
                    {
                        information.Update(update);
                    }
                }
            }

            private void OnRemoved(DeviceWatcher sender, DeviceInformationUpdate update)
            {
                lock (sync)
                {
                    if (!disposed)
                    {
                        devices.Remove(update.Id);
                    }
                }
            }

            private void OnEnumerationCompleted(DeviceWatcher sender, object eventArgs)
            {
                enumerationCompleted.TrySetResult(true);
            }

            private void OnStopped(DeviceWatcher sender, object eventArgs)
            {
                if (!disposed)
                {
                    enumerationCompleted.TrySetResult(false);
                }
            }

            public void Dispose()
            {
                if (disposed)
                {
                    return;
                }

                disposed = true;

                try
                {
                    watcher.Added -= addedHandler;
                    watcher.Updated -= updatedHandler;
                    watcher.Removed -= removedHandler;
                    watcher.EnumerationCompleted -= enumerationCompletedHandler;
                    watcher.Stopped -= stoppedHandler;
                }
                catch (Exception)
                {
                }

                if (watcher.Status == DeviceWatcherStatus.Started || watcher.Status == DeviceWatcherStatus.EnumerationCompleted)
                {
                    try
                    {
                        watcher.Stop();
                    }
                    catch (Exception)
                    {
                    }
                }

                lock (sync)
                {
                    devices.Clear();
                }
            }
        }

        private sealed class PairedDeviceCandidate
        {
            public string Id { get; private set; }
            public string Name { get; private set; }
            public bool? IsPresent { get; private set; }
            public bool? IsConnectable { get; private set; }

            public PairedDeviceCandidate(string id, string name, bool? isPresent, bool? isConnectable)
            {
                Id = id;
                Name = name;
                IsPresent = isPresent;
                IsConnectable = isConnectable;
            }

            public int ConnectionRank
            {
                get
                {
                    if (IsPresent == true && IsConnectable == true)
                    {
                        return 0;
                    }
                    if (IsPresent == false || IsConnectable == false)
                    {
                        return 2;
                    }

                    return 1;
                }
            }
        }

        private sealed class ConnectionResult
        {
            public string DeviceId { get; private set; }
            public BleConnection Connection { get; private set; }

            public ConnectionResult(string deviceId, BleConnection connection)
            {
                DeviceId = deviceId;
                Connection = connection;
            }
        }

        private sealed class ScanOutcome
        {
            public bool HasAddress { get; private set; }
            public ulong Address { get; private set; }
            public BluetoothAddressType AddressType { get; private set; }

            private ScanOutcome(bool hasAddress, ulong address, BluetoothAddressType addressType)
            {
                HasAddress = hasAddress;
                Address = address;
                AddressType = addressType;
            }

            public static ScanOutcome Found(ulong address, BluetoothAddressType addressType)
            {
                return new ScanOutcome(true, address, addressType);
            }

            public static ScanOutcome Stopped()
            {
                return new ScanOutcome(false, 0, BluetoothAddressType.Unspecified);
            }
        }

        private sealed class BleConnection : IDisposable
        {
            private readonly object sync = new object();
            private readonly BluetoothLEDevice device;
            private readonly GattSession session;
            private readonly TypedEventHandler<BluetoothLEDevice, object> connectionStatusHandler;
            private readonly TypedEventHandler<BluetoothLEDevice, object> servicesChangedHandler;
            private TaskCompletionSource<bool> stateChanged;
            private GattDeviceService service;
            private GattCharacteristic statusCharacteristic;
            private volatile bool active;
            private volatile bool reconnectRequired;
            private volatile bool servicesChanged;
            private bool disposed;

            public BleConnection(BluetoothLEDevice device, GattSession session)
            {
                this.device = device;
                this.session = session;
                stateChanged = CreateSignal();
                connectionStatusHandler = OnConnectionStatusChanged;
                servicesChangedHandler = OnServicesChanged;
                device.ConnectionStatusChanged += connectionStatusHandler;

                try
                {
                    device.GattServicesChanged += servicesChangedHandler;
                }
                catch
                {
                    device.ConnectionStatusChanged -= connectionStatusHandler;
                    throw;
                }
            }

            public GattCharacteristic StatusCharacteristic
            {
                get
                {
                    if (!active || statusCharacteristic == null)
                    {
                        throw new InvalidOperationException("Connection is not active.");
                    }

                    return statusCharacteristic;
                }
            }

            public bool NeedsReconnect
            {
                get { return active && reconnectRequired; }
            }

            public bool IsDeviceConnected
            {
                get { return device.ConnectionStatus == BluetoothConnectionStatus.Connected; }
            }

            public bool ServicesChanged
            {
                get { return active && servicesChanged; }
            }

            public void Activate(GattDeviceService service, GattCharacteristic statusCharacteristic)
            {
                if (disposed)
                {
                    throw new ObjectDisposedException("BleConnection");
                }

                this.service = service;
                this.statusCharacteristic = statusCharacteristic;
                active = true;
            }

            public void MarkDisconnected()
            {
                if (active)
                {
                    reconnectRequired = true;
                    SignalStateChanged();
                }
            }

            public async Task<bool> WaitForConnectedAsync(TimeSpan timeout, CancellationToken cancellationToken)
            {
                DateTime deadlineUtc = DateTime.UtcNow + timeout;

                while (true)
                {
                    cancellationToken.ThrowIfCancellationRequested();
                    Task signalTask;

                    lock (sync)
                    {
                        if (disposed || servicesChanged)
                        {
                            return false;
                        }

                        if (device.ConnectionStatus == BluetoothConnectionStatus.Connected)
                        {
                            return true;
                        }

                        signalTask = stateChanged.Task;
                    }

                    TimeSpan remaining = deadlineUtc - DateTime.UtcNow;
                    if (remaining <= TimeSpan.Zero)
                    {
                        return false;
                    }

                    Task timeoutTask = Task.Delay(remaining, cancellationToken);
                    Task completedTask = await Task.WhenAny(signalTask, timeoutTask).ConfigureAwait(false);

                    if (completedTask == timeoutTask)
                    {
                        cancellationToken.ThrowIfCancellationRequested();
                        return false;
                    }
                }
            }

            private void OnConnectionStatusChanged(BluetoothLEDevice sender, object eventArgs)
            {
                bool disconnected;

                try
                {
                    disconnected = sender.ConnectionStatus != BluetoothConnectionStatus.Connected;
                }
                catch (Exception)
                {
                    disconnected = true;
                }

                if (active && disconnected)
                {
                    reconnectRequired = true;
                }

                SignalStateChanged();
            }

            private void OnServicesChanged(BluetoothLEDevice sender, object eventArgs)
            {
                if (active)
                {
                    servicesChanged = true;
                    SignalStateChanged();
                }
            }

            private static TaskCompletionSource<bool> CreateSignal()
            {
                return new TaskCompletionSource<bool>(TaskCreationOptions.RunContinuationsAsynchronously);
            }

            private void SignalStateChanged()
            {
                TaskCompletionSource<bool> completedSignal;

                lock (sync)
                {
                    completedSignal = stateChanged;
                    stateChanged = CreateSignal();
                }

                completedSignal.TrySetResult(true);
            }

            public void Dispose()
            {
                if (disposed)
                {
                    return;
                }

                disposed = true;
                active = false;

                try
                {
                    device.ConnectionStatusChanged -= connectionStatusHandler;
                }
                catch (Exception)
                {
                }

                try
                {
                    device.GattServicesChanged -= servicesChangedHandler;
                }
                catch (Exception)
                {
                }

                SignalStateChanged();

                statusCharacteristic = null;

                if (service != null)
                {
                    SafeDisposeService(service);
                    service = null;
                }

                try
                {
                    session.MaintainConnection = false;
                }
                catch (Exception)
                {
                }

                try
                {
                    session.Dispose();
                }
                catch (Exception)
                {
                }

                try
                {
                    device.Dispose();
                }
                catch (Exception)
                {
                }
            }
        }

        private sealed class PairingRequiredException : Exception
        {
            public PairingRequiredException(string message)
                : base(message)
            {
            }
        }

        private sealed class FatalBridgeException : Exception
        {
            public bool IsGlobal { get; private set; }

            public FatalBridgeException(string message)
                : this(message, false)
            {
            }

            public FatalBridgeException(string message, bool isGlobal)
                : base(message)
            {
                IsGlobal = isGlobal;
            }
        }

        private sealed class BridgeException : Exception
        {
            public BridgeException(string message)
                : base(message)
            {
            }
        }
    }
}
