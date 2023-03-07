#include <gtest/gtest.h>

#include "isobus/hardware_integration/can_hardware_interface.hpp"
#include "isobus/hardware_integration/virtual_can_plugin.hpp"
#include "isobus/isobus/can_network_manager.hpp"
#include "isobus/isobus/isobus_task_controller_client.hpp"
#include "isobus/utility/system_timing.hpp"

using namespace isobus;

class DerivedTestTCClient : public TaskControllerClient
{
public:
	DerivedTestTCClient(std::shared_ptr<PartneredControlFunction> partner, std::shared_ptr<InternalControlFunction> clientSource) :
	  TaskControllerClient(partner, clientSource, nullptr){};

	bool test_wrapper_send_working_set_master() const
	{
		return TaskControllerClient::send_working_set_master();
	}

	void test_wrapper_set_state(TaskControllerClient::StateMachineState newState)
	{
		TaskControllerClient::set_state(newState);
	}

	void test_wrapper_set_state(TaskControllerClient::StateMachineState newState, std::uint32_t timestamp_ms)
	{
		TaskControllerClient::set_state(newState, timestamp_ms);
	}

	TaskControllerClient::StateMachineState test_wrapper_get_state() const
	{
		return TaskControllerClient::get_state();
	}

	bool test_wrapper_send_version_request() const
	{
		return TaskControllerClient::send_version_request();
	}

	bool test_wrapper_send_request_version_response() const
	{
		return TaskControllerClient::send_request_version_response();
	}

	bool test_wrapper_send_request_structure_label() const
	{
		return TaskControllerClient::send_request_structure_label();
	}

	bool test_wrapper_send_request_localization_label() const
	{
		return TaskControllerClient::send_request_localization_label();
	}

	bool test_wrapper_send_delete_object_pool() const
	{
		return TaskControllerClient::send_delete_object_pool();
	}

	bool test_wrapper_send_pdack(std::uint16_t elementNumber, std::uint16_t ddi) const
	{
		return TaskControllerClient::send_pdack(elementNumber, ddi);
	}

	bool test_wrapper_send_value_command(std::uint16_t elementNumber, std::uint16_t ddi, std::uint32_t value) const
	{
		return TaskControllerClient::send_value_command(elementNumber, ddi, value);
	}

	bool test_wrapper_process_internal_object_pool_upload_callback(std::uint32_t callbackIndex,
	                                                               std::uint32_t bytesOffset,
	                                                               std::uint32_t numberOfBytesNeeded,
	                                                               std::uint8_t *chunkBuffer,
	                                                               void *parentPointer)
	{
		return TaskControllerClient::process_internal_object_pool_upload_callback(callbackIndex, bytesOffset, numberOfBytesNeeded, chunkBuffer, parentPointer);
	}

	void test_wrapper_process_tx_callback(std::uint32_t parameterGroupNumber,
	                                      std::uint32_t dataLength,
	                                      InternalControlFunction *sourceControlFunction,
	                                      ControlFunction *destinationControlFunction,
	                                      bool successful,
	                                      void *parentPointer)
	{
		TaskControllerClient::process_tx_callback(parameterGroupNumber, dataLength, sourceControlFunction, destinationControlFunction, successful, parentPointer);
	}
};

TEST(TASK_CONTROLLER_CLIENT_TESTS, MessageEncoding)
{
	VirtualCANPlugin serverTC;
	serverTC.open();
	auto blankDDOP = std::make_shared<DeviceDescriptorObjectPool>();

	CANHardwareInterface::set_number_of_can_channels(1);
	CANHardwareInterface::assign_can_channel_frame_handler(0, std::make_shared<VirtualCANPlugin>());
	CANHardwareInterface::add_can_lib_update_callback(
	  [] {
		  CANNetworkManager::CANNetwork.update();
	  },
	  nullptr);
	CANHardwareInterface::start();

	NAME clientNAME(0);
	clientNAME.set_industry_group(2);
	clientNAME.set_function_code(static_cast<std::uint8_t>(NAME::Function::RateControl));
	auto internalECU = std::make_shared<InternalControlFunction>(clientNAME, 0x81, 0);

	HardwareInterfaceCANFrame testFrame;

	std::uint32_t waitingTimestamp_ms = SystemTiming::get_timestamp_ms();

	while ((!internalECU->get_address_valid()) &&
	       (!SystemTiming::time_expired_ms(waitingTimestamp_ms, 2000)))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	ASSERT_TRUE(internalECU->get_address_valid());

	std::vector<isobus::NAMEFilter> vtNameFilters;
	const isobus::NAMEFilter testFilter(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	vtNameFilters.push_back(testFilter);

	auto vtPartner = std::make_shared<PartneredControlFunction>(0, vtNameFilters);

	// Force claim a partner
	testFrame.dataLength = 8;
	testFrame.channel = 0;
	testFrame.isExtendedFrame = true;
	testFrame.identifier = 0x18EEFFF7;
	testFrame.data[0] = 0x03;
	testFrame.data[1] = 0x04;
	testFrame.data[2] = 0x00;
	testFrame.data[3] = 0x12;
	testFrame.data[4] = 0x00;
	testFrame.data[5] = 0x82;
	testFrame.data[6] = 0x00;
	testFrame.data[7] = 0xA0;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);

	DerivedTestTCClient interfaceUnderTest(vtPartner, internalECU);

	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// Get the virtual CAN plugin back to a known state
	while (!serverTC.get_queue_empty())
	{
		serverTC.read_frame(testFrame);
	}
	ASSERT_TRUE(serverTC.get_queue_empty());

	// Test Working Set Master Message
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_working_set_master());

	ASSERT_TRUE(serverTC.read_frame(testFrame));

	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xFE0D);
	EXPECT_EQ(testFrame.data[0], 1); // 1 Working set member by default

	for (std::uint_fast8_t i = 1; i < 8; i++)
	{
		// Check Reserved Bytes
		ASSERT_EQ(testFrame.data[i], 0xFF);
	}

	// Test Version Request Message
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_version_request());

	ASSERT_TRUE(serverTC.read_frame(testFrame));

	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0x00, testFrame.data[0]);

	for (std::uint_fast8_t i = 1; i < 8; i++)
	{
		// Check Reserved Bytes
		ASSERT_EQ(testFrame.data[i], 0xFF);
	}

	// Test status message
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendStatusMessage);
	ASSERT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendStatusMessage);
	interfaceUnderTest.update();

	serverTC.read_frame(testFrame);

	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0xFF, testFrame.data[0]); // Mux
	EXPECT_EQ(0xFF, testFrame.data[1]); // Element number
	EXPECT_EQ(0xFF, testFrame.data[2]); // DDI
	EXPECT_EQ(0xFF, testFrame.data[3]); // DDI
	EXPECT_EQ(0x00, testFrame.data[4]); // Status
	EXPECT_EQ(0x00, testFrame.data[5]); // 0 Reserved
	EXPECT_EQ(0x00, testFrame.data[6]); // 0 Reserved
	EXPECT_EQ(0x00, testFrame.data[7]); // 0 Reserved

	// Test version response
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_request_version_response());
	serverTC.read_frame(testFrame);
	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0x10, testFrame.data[0]); // Mux
	EXPECT_EQ(0x03, testFrame.data[1]); // Version ///!todo fix version
	EXPECT_EQ(0xFF, testFrame.data[2]); // Must be 0xFF
	EXPECT_EQ(0x00, testFrame.data[3]); // Options
	EXPECT_EQ(0x00, testFrame.data[4]); // Must be zero
	EXPECT_EQ(0x00, testFrame.data[5]); // Booms
	EXPECT_EQ(0x00, testFrame.data[6]); // Sections
	EXPECT_EQ(0x00, testFrame.data[7]); // Channels

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::Disconnected);
	interfaceUnderTest.configure(blankDDOP, 1, 2, 3, true, true, true, true, true);
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_request_version_response());
	serverTC.read_frame(testFrame);

	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0x10, testFrame.data[0]); // Mux
	EXPECT_EQ(0x03, testFrame.data[1]); // Version ///!todo fix version
	EXPECT_EQ(0xFF, testFrame.data[2]); // Must be 0xFF
	EXPECT_EQ(0x1F, testFrame.data[3]); // Options
	EXPECT_EQ(0x00, testFrame.data[4]); // Must be zero
	EXPECT_EQ(0x01, testFrame.data[5]); // Booms
	EXPECT_EQ(0x02, testFrame.data[6]); // Sections
	EXPECT_EQ(0x03, testFrame.data[7]); // Channels

	// Test Request structure label
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_request_structure_label());
	serverTC.read_frame(testFrame);
	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0x01, testFrame.data[0]);
	for (std::uint_fast8_t i = 1; i < 7; i++)
	{
		EXPECT_EQ(0xFF, testFrame.data[i]);
	}

	// Test Request localization label
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_request_localization_label());
	serverTC.read_frame(testFrame);
	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0x21, testFrame.data[0]);
	for (std::uint_fast8_t i = 1; i < 7; i++)
	{
		EXPECT_EQ(0xFF, testFrame.data[i]);
	}

	// Test Delete Object Pool
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_delete_object_pool());
	serverTC.read_frame(testFrame);
	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0xA1, testFrame.data[0]);
	for (std::uint_fast8_t i = 1; i < 7; i++)
	{
		EXPECT_EQ(0xFF, testFrame.data[i]);
	}

	// Test PDACK
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_pdack(47, 29));
	serverTC.read_frame(testFrame);
	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0xFD, testFrame.data[0]);
	EXPECT_EQ(0x02, testFrame.data[1]);
	EXPECT_EQ(0x1D, testFrame.data[2]);
	EXPECT_EQ(0x00, testFrame.data[3]);

	// Test Value Command
	ASSERT_TRUE(interfaceUnderTest.test_wrapper_send_value_command(1234, 567, 8910));
	serverTC.read_frame(testFrame);
	ASSERT_TRUE(testFrame.isExtendedFrame);
	ASSERT_EQ(testFrame.dataLength, 8);
	EXPECT_EQ(CANIdentifier(testFrame.identifier).get_parameter_group_number(), 0xCB00);
	EXPECT_EQ(0x23, testFrame.data[0]);
	EXPECT_EQ(0x4D, testFrame.data[1]);
	EXPECT_EQ(0x37, testFrame.data[2]);
	EXPECT_EQ(0x02, testFrame.data[3]);
	EXPECT_EQ(0xCE, testFrame.data[4]);
	EXPECT_EQ(0x22, testFrame.data[5]);
	EXPECT_EQ(0x00, testFrame.data[6]);
	EXPECT_EQ(0x00, testFrame.data[7]);

	CANHardwareInterface::stop();
	CANHardwareInterface::set_number_of_can_channels(0);
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, BadPartnerDeathTest)
{
	NAME clientNAME(0);
	clientNAME.set_industry_group(2);
	clientNAME.set_function_code(static_cast<std::uint8_t>(NAME::Function::RateControl));
	auto internalECU = std::make_shared<InternalControlFunction>(clientNAME, 0x81, 0);
	DerivedTestTCClient interfaceUnderTest(nullptr, internalECU);
	ASSERT_FALSE(interfaceUnderTest.get_is_initialized());
	EXPECT_DEATH(interfaceUnderTest.initialize(false), "");
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, BadICFDeathTest)
{
	std::vector<isobus::NAMEFilter> vtNameFilters;
	const isobus::NAMEFilter testFilter(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	vtNameFilters.push_back(testFilter);

	auto vtPartner = std::make_shared<PartneredControlFunction>(0, vtNameFilters);
	DerivedTestTCClient interfaceUnderTest(vtPartner, nullptr);
	ASSERT_FALSE(interfaceUnderTest.get_is_initialized());
	EXPECT_DEATH(interfaceUnderTest.initialize(false), "");
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, StateMachineTests)
{
	// Boilerplate...
	VirtualCANPlugin serverTC;
	serverTC.open();

	CANHardwareInterface::set_number_of_can_channels(1);
	CANHardwareInterface::assign_can_channel_frame_handler(0, std::make_shared<VirtualCANPlugin>());
	CANHardwareInterface::add_can_lib_update_callback(
	  [] {
		  CANNetworkManager::CANNetwork.update();
	  },
	  nullptr);
	CANHardwareInterface::start();

	NAME clientNAME(0);
	clientNAME.set_industry_group(2);
	clientNAME.set_ecu_instance(1);
	clientNAME.set_function_code(static_cast<std::uint8_t>(NAME::Function::RateControl));
	auto internalECU = std::make_shared<InternalControlFunction>(clientNAME, 0x83, 0);

	HardwareInterfaceCANFrame testFrame;

	std::uint32_t waitingTimestamp_ms = SystemTiming::get_timestamp_ms();

	while ((!internalECU->get_address_valid()) &&
	       (!SystemTiming::time_expired_ms(waitingTimestamp_ms, 2000)))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	ASSERT_TRUE(internalECU->get_address_valid());

	std::vector<isobus::NAMEFilter> tcNameFilters;
	const isobus::NAMEFilter testFilter(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	tcNameFilters.push_back(testFilter);

	auto tcPartner = std::make_shared<PartneredControlFunction>(0, tcNameFilters);

	// Force claim a partner
	testFrame.dataLength = 8;
	testFrame.channel = 0;
	testFrame.isExtendedFrame = true;
	testFrame.identifier = 0x18EEFFF7;
	testFrame.data[0] = 0x03;
	testFrame.data[1] = 0x04;
	testFrame.data[2] = 0x00;
	testFrame.data[3] = 0x12;
	testFrame.data[4] = 0x00;
	testFrame.data[5] = 0x82;
	testFrame.data[6] = 0x00;
	testFrame.data[7] = 0xA0;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);

	DerivedTestTCClient interfaceUnderTest(tcPartner, internalECU);
	interfaceUnderTest.initialize(false);

	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// Get the virtual CAN plugin back to a known state
	while (!serverTC.get_queue_empty())
	{
		serverTC.read_frame(testFrame);
	}
	ASSERT_TRUE(serverTC.get_queue_empty());

	// End boilerplate

	// Check initial state
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Check Transition out of status message wait state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForServerStatusMessage);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForServerStatusMessage);

	// Send a status message and confirm we move on to the next state.
	testFrame.identifier = 0x18CBFFF7;
	testFrame.data[0] = 0xFE; // Status mux
	testFrame.data[1] = 0xFF; // Element number, set to not available
	testFrame.data[2] = 0xFF; // DDI (N/A)
	testFrame.data[3] = 0xFF; // DDI (N/A)
	testFrame.data[4] = 0x01; // Status (task active)
	testFrame.data[5] = 0x00; // Command address
	testFrame.data[6] = 0x00; // Command
	testFrame.data[7] = 0xFF; // Reserved
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);

	CANNetworkManager::CANNetwork.update();

	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendWorkingSetMaster);

	// Test Send Working Set Master State
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendStatusMessage);

	// Test Request Language state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::RequestLanguage);
	interfaceUnderTest.update();

	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLanguageResponse);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForLanguageResponse, 0);

	// Test wait for language response state
	testFrame.identifier = 0x18FE0FF7;
	testFrame.data[0] = 'e';
	testFrame.data[1] = 'n',
	testFrame.data[2] = 0b00001111;
	testFrame.data[3] = 0x04;
	testFrame.data[4] = 0b01011010;
	testFrame.data[5] = 0b00000100;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::ProcessDDOP);

	// Test Version Response State
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestVersionResponse);
	interfaceUnderTest.update();

	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestVersionResponse);

	// Send the version response to the client as the TC server
	// Send a status message and confirm we move on to the next state.
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x10; // Mux
	testFrame.data[1] = 0x04; // Version number (Version 4)
	testFrame.data[2] = 0xFF; // Max boot time (Not available)
	testFrame.data[3] = 0b0011111; // Supports all options
	testFrame.data[4] = 0x00; // Reserved options = 0
	testFrame.data[5] = 0x01; // Number of booms for section control (1)
	testFrame.data[6] = 0x20; // Number of sections for section control (32)
	testFrame.data[7] = 0x10; // Number channels for position based control (16)
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);

	CANNetworkManager::CANNetwork.update();

	// Test the values parsed in this state machine state
	EXPECT_EQ(TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer, interfaceUnderTest.test_wrapper_get_state());
	EXPECT_EQ(TaskControllerClient::Version::SecondPublishedEdition, interfaceUnderTest.get_connected_tc_version());
	EXPECT_EQ(0xFF, interfaceUnderTest.get_connected_tc_max_boot_time());
	EXPECT_EQ(true, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::SupportsDocumentation));
	EXPECT_EQ(true, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::SupportsImplementSectionControlFunctionality));
	EXPECT_EQ(true, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::SupportsPeerControlAssignment));
	EXPECT_EQ(true, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::SupportsTCGEOWithPositionBasedControl));
	EXPECT_EQ(true, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::SupportsTCGEOWithoutPositionBasedControl));
	EXPECT_EQ(false, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::ReservedOption1));
	EXPECT_EQ(false, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::ReservedOption2));
	EXPECT_EQ(false, interfaceUnderTest.get_connected_tc_option_supported(TaskControllerClient::ServerOptions::ReservedOption3));
	EXPECT_EQ(1, interfaceUnderTest.get_connected_tc_number_booms_supported());
	EXPECT_EQ(32, interfaceUnderTest.get_connected_tc_number_sections_supported());
	EXPECT_EQ(16, interfaceUnderTest.get_connected_tc_number_channels_supported());

	// Test Status Message State
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendStatusMessage);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendStatusMessage);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestVersion);

	// Test transition to disconnect from NACK
	// Send a NACK
	testFrame.identifier = 0x18E883F7;
	testFrame.data[0] = 0x01; // N-ACK
	testFrame.data[1] = 0xFF;
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0x83; // Address
	testFrame.data[5] = 0x00; // PGN
	testFrame.data[6] = 0xCB; // PGN
	testFrame.data[7] = 0x00; // PGN
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test send structure request state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::RequestStructureLabel);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestStructureLabel);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);

	// Test send localization request state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::RequestLocalizationLabel);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestLocalizationLabel);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);

	// Test send delete object pool states
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendDeleteObjectPool);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendDeleteObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDeleteObjectPoolResponse);
	// Send a response
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0xB1; // Mux
	testFrame.data[1] = 0xFF; // Ambigious
	testFrame.data[2] = 0xFF; // Ambigious
	testFrame.data[3] = 0xFF; // error details are not available
	testFrame.data[4] = 0xFF; // Reserved
	testFrame.data[5] = 0xFF; // Reserved
	testFrame.data[6] = 0xFF; // Reserved
	testFrame.data[7] = 0xFF; // Reserved
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestTransferObjectPool);

	// Test send activate object pool state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendObjectPoolActivate);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);

	// Test send deactivate object pool state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::DeactivateObjectPool);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::DeactivateObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolDeactivateResponse);

	// Test task state when not connected
	EXPECT_EQ(false, interfaceUnderTest.get_is_task_active());

	// Test Connected State
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::Connected);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Connected);
	EXPECT_EQ(true, interfaceUnderTest.get_is_connected());
	EXPECT_EQ(true, interfaceUnderTest.get_is_task_active());

	// Test WaitForRequestVersionFromServer State
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer);
	// Send a request for version
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x00; // Mux
	testFrame.data[1] = 0xFF; // Reserved
	testFrame.data[2] = 0xFF; // Reserved
	testFrame.data[3] = 0xFF; // Reserved
	testFrame.data[4] = 0xFF; // Reserved
	testFrame.data[5] = 0xFF; // Reserved
	testFrame.data[6] = 0xFF; // Reserved
	testFrame.data[7] = 0xFF; // Reserved
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestVersionResponse);
	// Test strange technical command doesn't change the state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer);
	// Send a request for version
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x40; // Mux
	testFrame.data[1] = 0xFF; // Reserved
	testFrame.data[2] = 0xFF; // Reserved
	testFrame.data[3] = 0xFF; // Reserved
	testFrame.data[4] = 0xFF; // Reserved
	testFrame.data[5] = 0xFF; // Reserved
	testFrame.data[6] = 0xFF; // Reserved
	testFrame.data[7] = 0xFF; // Reserved
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer);

	// Test WaitForStructureLabelResponse State
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	// Send a request for version
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x11; // Mux
	testFrame.data[1] = 0xFF; // No Label
	testFrame.data[2] = 0xFF; // No Label
	testFrame.data[3] = 0xFF; // No Label
	testFrame.data[4] = 0xFF; // No Label
	testFrame.data[5] = 0xFF; // No Label
	testFrame.data[6] = 0xFF; // No Label
	testFrame.data[7] = 0xFF; // No Label
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestTransferObjectPool);

	// Test generating a null DDOP
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::ProcessDDOP);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::ProcessDDOP);
	EXPECT_DEATH(interfaceUnderTest.update(), "");

	// Need a DDOP to test some states...
	auto testDDOP = std::make_shared<DeviceDescriptorObjectPool>();
	ASSERT_NE(nullptr, testDDOP);

	// Make a test pool, don't care about our ISO NAME, Localization label, or extended structure label for this test
	// Set up device
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::Disconnected);
	ASSERT_EQ(true, testDDOP->add_device("Isobus++ UnitTest", "1.0.0", "123", "I++1.0", { 0x01 }, std::vector<std::uint8_t>(), 0));
	interfaceUnderTest.configure(testDDOP, 6, 64, 32, false, false, false, false, false);

	// Now try it with a valid structure label
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	// Send a structure label
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x11; // Mux
	testFrame.data[1] = 0x04; // A valid label technically
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0xFF;
	testFrame.data[5] = 0xFF;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendDeleteObjectPool);

	// Now try it with a matching structure label
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	// Send a structure label
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x11; // Mux
	testFrame.data[1] = 'I';
	testFrame.data[2] = '+';
	testFrame.data[3] = '+';
	testFrame.data[4] = '1';
	testFrame.data[5] = '.';
	testFrame.data[6] = '0';
	testFrame.data[7] = ' ';
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestLocalizationLabel);

	// Test wait for localization label response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	// Send a localization label
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x31; // Mux
	testFrame.data[1] = 0xFF; // A bad label, since all 0xFFs
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0xFF;
	testFrame.data[5] = 0xFF;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestTransferObjectPool);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	// Send a localization label that doesn't match the stored one
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x31; // Mux
	testFrame.data[1] = 0x01; // A valid label
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0xFF;
	testFrame.data[5] = 0xFF;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendDeleteObjectPool);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	// Send a localization label that doesn't match the stored one
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x31; // Mux
	testFrame.data[1] = 0x01; // A matching label
	testFrame.data[2] = 0x00;
	testFrame.data[3] = 0x00;
	testFrame.data[4] = 0x00;
	testFrame.data[5] = 0x00;
	testFrame.data[6] = 0x00;
	testFrame.data[7] = 0x00;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	// Check ddop transfer callback
	interfaceUnderTest.test_wrapper_process_tx_callback(0xCB00, 8, nullptr, reinterpret_cast<ControlFunction *>(tcPartner.get()), false, &interfaceUnderTest);
	// In this case it should be disconnected because we passed in false.
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	// Check ddop transfer callback
	interfaceUnderTest.test_wrapper_process_tx_callback(0xCB00, 8, nullptr, reinterpret_cast<ControlFunction *>(tcPartner.get()), true, &interfaceUnderTest);
	// In this case it should be disconnected because we passed in false.
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);

	// Test wait for object pool transfer response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	// Send a response with good data
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x71; // Mux
	testFrame.data[1] = 0x00;
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0xFF;
	testFrame.data[5] = 0xFF;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	// Send a response with bad data
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x71; // Mux
	testFrame.data[1] = 0x01; // Ran out of memory!
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_NE(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);
	interfaceUnderTest.initialize(false); // Fix the interface after terminate was called

	// Test wait for request object pool transfer response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse);
	// Send a response with good data
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x51; // Mux
	testFrame.data[1] = 0x00;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::BeginTransferDDOP);

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse);
	// Send a response with bad data
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x51; // Mux
	testFrame.data[1] = 0x01; // Not enough memory!
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_NE(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::BeginTransferDDOP);
	interfaceUnderTest.initialize(false); // Fix the interface after terminate was called

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendRequestVersionResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestVersionResponse);
	interfaceUnderTest.update(); // Update the state, should go to the request language state
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestLanguage);

	// Test generating a valid DDOP
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::ProcessDDOP);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::ProcessDDOP);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestStructureLabel);

	// Do the DDOP generation again
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::ProcessDDOP);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::ProcessDDOP);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestStructureLabel);

	// Switch to a trash DDOP
	auto testJunkDDOP = std::make_shared<DeviceDescriptorObjectPool>();
	ASSERT_NE(nullptr, testJunkDDOP);
	testJunkDDOP->add_device_property("aksldfjhalkf", 1, 6, 123, 456);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::Disconnected);
	interfaceUnderTest.configure(testJunkDDOP, 32, 32, 32, true, true, true, true, true);

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::ProcessDDOP);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::ProcessDDOP);
	interfaceUnderTest.update();
	interfaceUnderTest.initialize(false); // Fix after terminate gets called.

	// Test sending request for object pool
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendRequestTransferObjectPool);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestTransferObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse);

	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x91; // Mux
	testFrame.data[1] = 0x00; // It worked!
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0xFF;
	testFrame.data[5] = 0xFF;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Connected);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	testFrame.identifier = 0x18CB83F7;
	testFrame.data[0] = 0x91; // Mux
	testFrame.data[1] = 0x01; // It didn't work!
	testFrame.data[2] = 0xFF;
	testFrame.data[3] = 0xFF;
	testFrame.data[4] = 0xFF;
	testFrame.data[5] = 0xFF;
	testFrame.data[6] = 0xFF;
	testFrame.data[7] = 0xFF;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	EXPECT_NE(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Connected);

	//! @Todo Add other states

	// Test invalid state gets caught by assert
	interfaceUnderTest.test_wrapper_set_state(static_cast<TaskControllerClient::StateMachineState>(241));
	EXPECT_DEATH(interfaceUnderTest.update(), "");

	interfaceUnderTest.terminate();
	CANHardwareInterface::stop();
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, ClientSettings)
{
	DerivedTestTCClient interfaceUnderTest(nullptr, nullptr);
	auto blankDDOP = std::make_shared<DeviceDescriptorObjectPool>();

	// Set and test the basic settings for the client
	interfaceUnderTest.configure(blankDDOP, 6, 64, 32, false, false, false, false, false);
	EXPECT_EQ(6, interfaceUnderTest.get_number_booms_supported());
	EXPECT_EQ(64, interfaceUnderTest.get_number_sections_supported());
	EXPECT_EQ(32, interfaceUnderTest.get_number_channels_supported_for_position_based_control());
	EXPECT_EQ(false, interfaceUnderTest.get_supports_documentation());
	EXPECT_EQ(false, interfaceUnderTest.get_supports_implement_section_control());
	EXPECT_EQ(false, interfaceUnderTest.get_supports_peer_control_assignment());
	EXPECT_EQ(false, interfaceUnderTest.get_supports_tcgeo_without_position_based_control());
	EXPECT_EQ(false, interfaceUnderTest.get_supports_tcgeo_with_position_based_control());
	interfaceUnderTest.configure(blankDDOP, 255, 255, 255, true, true, true, true, true);
	EXPECT_EQ(255, interfaceUnderTest.get_number_booms_supported());
	EXPECT_EQ(255, interfaceUnderTest.get_number_sections_supported());
	EXPECT_EQ(255, interfaceUnderTest.get_number_channels_supported_for_position_based_control());
	EXPECT_EQ(true, interfaceUnderTest.get_supports_documentation());
	EXPECT_EQ(true, interfaceUnderTest.get_supports_implement_section_control());
	EXPECT_EQ(true, interfaceUnderTest.get_supports_peer_control_assignment());
	EXPECT_EQ(true, interfaceUnderTest.get_supports_tcgeo_without_position_based_control());
	EXPECT_EQ(true, interfaceUnderTest.get_supports_tcgeo_with_position_based_control());
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, TimeoutTests)
{
	NAME clientNAME(0);
	clientNAME.set_industry_group(2);
	clientNAME.set_ecu_instance(1);
	clientNAME.set_function_code(static_cast<std::uint8_t>(NAME::Function::RateControl));
	auto internalECU = std::make_shared<InternalControlFunction>(clientNAME, 0x84, 0);

	ASSERT_FALSE(internalECU->get_address_valid());

	std::vector<isobus::NAMEFilter> vtNameFilters;
	const isobus::NAMEFilter testFilter(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	vtNameFilters.push_back(testFilter);

	auto vtPartner = std::make_shared<PartneredControlFunction>(0, vtNameFilters);

	DerivedTestTCClient interfaceUnderTest(vtPartner, internalECU);
	interfaceUnderTest.initialize(false);

	// Wait a while to build up some run time for testing timeouts later
	while (SystemTiming::get_timestamp_ms() < 6000)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
	}

	// Test disconnecting from trying to send working set master
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendWorkingSetMaster, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendWorkingSetMaster);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from trying to send status message
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendStatusMessage, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendStatusMessage);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from trying to send request version message
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::RequestVersion, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestVersion);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from trying to send request structure label message
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::RequestStructureLabel, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestStructureLabel);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from trying to send request localization label message
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::RequestLocalizationLabel, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestLocalizationLabel);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from waiting for request version response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestVersionResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestVersionResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from waiting for structure label response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForStructureLabelResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForStructureLabelResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting from sending delete object pool
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendDeleteObjectPool, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendDeleteObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting while waiting for object pool delete response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForDeleteObjectPoolResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDeleteObjectPoolResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting while waiting for sending request to transfer the DDOP
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendRequestTransferObjectPool, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestTransferObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test disconnecting while trying to send the DDOP
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::BeginTransferDDOP, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::BeginTransferDDOP);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test startup delay
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForStartUpDelay, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForStartUpDelay);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForServerStatusMessage);

	// Test no timeout when waiting for the status message initially
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForServerStatusMessage, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForServerStatusMessage);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForServerStatusMessage);

	// Test no timeout when waiting for Tx to complete. We will get a callback from transport layer for this
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForDDOPTransfer, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDDOPTransfer);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForDDOPTransfer);

	// Test timeout waiting for object pool transfer response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestTransferObjectPoolResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test timeout trying to send object pool activation
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendObjectPoolActivate, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendObjectPoolActivate);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendObjectPoolActivate);

	// Test timeout waiting to activate object pool
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolActivateResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test timeout while connected
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::Connected, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Connected);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test trying to deactivate object pool
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::DeactivateObjectPool, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::DeactivateObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::DeactivateObjectPool);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::DeactivateObjectPool);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::DeactivateObjectPool);

	// Test trying to deactivate object pool
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolDeactivateResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolDeactivateResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test timeout waiting for localization label response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLocalizationLabelResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Test timeout waiting for version request from server
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForRequestVersionFromServer);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::RequestLanguage);

	// Test lack of timeout waiting for language (hold state)
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForLanguageResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLanguageResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForLanguageResponse);

	// Test timeout waiting for object pool transfer response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);

	// Waiting for object pool transfer response hold state
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::WaitForObjectPoolTransferResponse);

	// Test timeout waiting to send request version response
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::SendRequestVersionResponse, 0);
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::SendRequestVersionResponse);
	interfaceUnderTest.update();
	EXPECT_EQ(interfaceUnderTest.test_wrapper_get_state(), TaskControllerClient::StateMachineState::Disconnected);
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, WorkerThread)
{
	NAME clientNAME(0);
	clientNAME.set_industry_group(2);
	clientNAME.set_ecu_instance(1);
	clientNAME.set_function_code(static_cast<std::uint8_t>(NAME::Function::RateControl));
	auto internalECU = std::make_shared<InternalControlFunction>(clientNAME, 0x85, 0);

	std::vector<isobus::NAMEFilter> vtNameFilters;
	const isobus::NAMEFilter testFilter(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	vtNameFilters.push_back(testFilter);

	auto vtPartner = std::make_shared<PartneredControlFunction>(0, vtNameFilters);

	DerivedTestTCClient interfaceUnderTest(vtPartner, internalECU);
	EXPECT_NO_THROW(interfaceUnderTest.initialize(true));

	EXPECT_NO_THROW(interfaceUnderTest.terminate());
}

static bool valueRequested = false;
static bool valueCommanded = false;
static std::uint16_t requestedDDI = 0;
static std::uint16_t commandedDDI = 0;
static std::uint16_t requestedElement = 0;
static std::uint16_t commandedElement = 0;
static std::uint32_t commandedValue = 0;

bool request_value_command_callback(std::uint16_t element,
                                    std::uint16_t ddi,
                                    std::uint32_t &,
                                    void *)
{
	requestedElement = element;
	requestedDDI = ddi;
	valueRequested = true;
	return true;
}

bool value_command_callback(std::uint16_t element,
                            std::uint16_t ddi,
                            std::uint32_t value,
                            void *)
{
	commandedElement = element;
	commandedDDI = ddi;
	valueCommanded = true;
	commandedValue = value;
	return true;
}

TEST(TASK_CONTROLLER_CLIENT_TESTS, CallbackTests)
{
	VirtualCANPlugin serverTC;
	serverTC.open();

	CANHardwareInterface::set_number_of_can_channels(1);
	CANHardwareInterface::assign_can_channel_frame_handler(0, std::make_shared<VirtualCANPlugin>());
	CANHardwareInterface::add_can_lib_update_callback(
	  [] {
		  CANNetworkManager::CANNetwork.update();
	  },
	  nullptr);
	CANHardwareInterface::start();

	NAME clientNAME(0);
	clientNAME.set_industry_group(2);
	clientNAME.set_ecu_instance(1);
	clientNAME.set_function_code(static_cast<std::uint8_t>(NAME::Function::RateControl));
	auto internalECU = std::make_shared<InternalControlFunction>(clientNAME, 0x86, 0);
	const isobus::NAMEFilter filterTaskController(isobus::NAME::NAMEParameters::FunctionCode, static_cast<std::uint8_t>(isobus::NAME::Function::TaskController));
	const std::vector<isobus::NAMEFilter> tcNameFilters = { filterTaskController };
	std::shared_ptr<isobus::PartneredControlFunction> TestPartnerTC = std::make_shared<isobus::PartneredControlFunction>(0, tcNameFilters);
	auto blankDDOP = std::make_shared<DeviceDescriptorObjectPool>();

	HardwareInterfaceCANFrame testFrame;

	// Force claim a partner
	testFrame.dataLength = 8;
	testFrame.channel = 0;
	testFrame.isExtendedFrame = true;
	testFrame.identifier = 0x18EEFFF7;
	testFrame.data[0] = 0x03;
	testFrame.data[1] = 0x04;
	testFrame.data[2] = 0x00;
	testFrame.data[3] = 0x13;
	testFrame.data[4] = 0x00;
	testFrame.data[5] = 0x82;
	testFrame.data[6] = 0x00;
	testFrame.data[7] = 0xA0;

	std::uint32_t waitingTimestamp_ms = SystemTiming::get_timestamp_ms();

	while ((!internalECU->get_address_valid()) &&
	       (!SystemTiming::time_expired_ms(waitingTimestamp_ms, 2000)))
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);

	DerivedTestTCClient interfaceUnderTest(TestPartnerTC, internalECU);
	interfaceUnderTest.initialize(false);

	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	// Get the virtual CAN plugin back to a known state
	while (!serverTC.get_queue_empty())
	{
		serverTC.read_frame(testFrame);
	}
	ASSERT_TRUE(serverTC.get_queue_empty());
	ASSERT_TRUE(TestPartnerTC->get_address_valid());

	// End boilerplate **********************************

	interfaceUnderTest.configure(blankDDOP, 1, 32, 32, true, false, true, false, true);
	interfaceUnderTest.add_request_value_callback(request_value_command_callback);
	interfaceUnderTest.add_value_command_callback(value_command_callback);
	interfaceUnderTest.test_wrapper_set_state(TaskControllerClient::StateMachineState::Connected);

	// Status message
	testFrame.identifier = 0x18CBFFF7;
	testFrame.data[0] = 0xFE; // Status mux
	testFrame.data[1] = 0xFF; // Element number, set to not available
	testFrame.data[2] = 0xFF; // DDI (N/A)
	testFrame.data[3] = 0xFF; // DDI (N/A)
	testFrame.data[4] = 0x01; // Status (task active)
	testFrame.data[5] = 0x00; // Command address
	testFrame.data[6] = 0x00; // Command
	testFrame.data[7] = 0xFF; // Reserved
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);

	// Create a request for a value.
	testFrame.identifier = 0x18CB86F7;
	testFrame.data[0] = 0x82;
	testFrame.data[1] = 0x04;
	testFrame.data[2] = 0x12;
	testFrame.data[3] = 0x34;
	testFrame.data[4] = 0x00;
	testFrame.data[5] = 0x00;
	testFrame.data[6] = 0x00;
	testFrame.data[7] = 0x00;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	interfaceUnderTest.update();

	// Ensure the values were passed through to the callback properly
	EXPECT_EQ(true, valueRequested);
	EXPECT_EQ(requestedDDI, 0x3412);
	EXPECT_EQ(requestedElement, 0x48);
	EXPECT_EQ(false, valueCommanded);
	EXPECT_EQ(commandedDDI, 0);
	EXPECT_EQ(commandedElement, 0);

	// Create a command for a value.
	testFrame.identifier = 0x18CB86F7;
	testFrame.data[0] = 0x83;
	testFrame.data[1] = 0x05;
	testFrame.data[2] = 0x19;
	testFrame.data[3] = 0x38;
	testFrame.data[4] = 0x01;
	testFrame.data[5] = 0x02;
	testFrame.data[6] = 0x03;
	testFrame.data[7] = 0x04;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	interfaceUnderTest.update();

	// Ensure the values were passed through to the callback properly
	EXPECT_EQ(true, valueCommanded);
	EXPECT_EQ(commandedDDI, 0x3819);
	EXPECT_EQ(commandedElement, 0x58);
	EXPECT_EQ(commandedValue, 0x4030201);
	EXPECT_EQ(true, valueRequested);
	EXPECT_EQ(requestedDDI, 0x3412);
	EXPECT_EQ(requestedElement, 0x48);

	// Set value and acknowledge
	testFrame.identifier = 0x18CB86F7;
	testFrame.data[0] = 0x2A;
	testFrame.data[1] = 0x05;
	testFrame.data[2] = 0x29;
	testFrame.data[3] = 0x48;
	testFrame.data[4] = 0x08;
	testFrame.data[5] = 0x07;
	testFrame.data[6] = 0x06;
	testFrame.data[7] = 0x05;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	interfaceUnderTest.update();

	EXPECT_EQ(true, valueCommanded);
	EXPECT_EQ(commandedDDI, 0x4829);
	EXPECT_EQ(commandedElement, 0x52);
	EXPECT_EQ(commandedValue, 0x5060708);
	EXPECT_EQ(true, valueRequested);
	EXPECT_EQ(requestedDDI, 0x3412);
	EXPECT_EQ(requestedElement, 0x48);

	valueRequested = false;
	requestedDDI = 0;
	requestedElement = 0;
	interfaceUnderTest.remove_request_value_callback(request_value_command_callback);

	// Create a request for a value.
	testFrame.identifier = 0x18CB86F7;
	testFrame.data[0] = 0x82;
	testFrame.data[1] = 0x04;
	testFrame.data[2] = 0x12;
	testFrame.data[3] = 0x34;
	testFrame.data[4] = 0x00;
	testFrame.data[5] = 0x00;
	testFrame.data[6] = 0x00;
	testFrame.data[7] = 0x00;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	interfaceUnderTest.update();
	// This time the callback should be gone.
	EXPECT_EQ(false, valueRequested);
	EXPECT_EQ(requestedDDI, 0);
	EXPECT_EQ(requestedElement, 0);
	EXPECT_EQ(true, valueCommanded);
	EXPECT_EQ(commandedDDI, 0x4829);
	EXPECT_EQ(commandedElement, 0x52);
	EXPECT_EQ(commandedValue, 0x5060708);

	valueCommanded = false;
	commandedDDI = 0;
	commandedElement = 0;
	commandedValue = 0x0;
	interfaceUnderTest.remove_value_command_callback(value_command_callback);

	// Create a command for a value.
	testFrame.identifier = 0x18CB86F7;
	testFrame.data[0] = 0x83;
	testFrame.data[1] = 0x05;
	testFrame.data[2] = 0x19;
	testFrame.data[3] = 0x38;
	testFrame.data[4] = 0x01;
	testFrame.data[5] = 0x02;
	testFrame.data[6] = 0x03;
	testFrame.data[7] = 0x04;
	CANNetworkManager::CANNetwork.can_lib_process_rx_message(testFrame, nullptr);
	CANNetworkManager::CANNetwork.update();
	interfaceUnderTest.update();

	// Now since the callback has been removed, no command should have happened
	EXPECT_EQ(false, valueCommanded);
	EXPECT_EQ(commandedDDI, 0);
	EXPECT_EQ(commandedElement, 0);
	EXPECT_EQ(commandedValue, 0);
}