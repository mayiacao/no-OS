/***************************************************************************//**
 *   @file   headless.c
 *   @brief  Implementation of Main Function.
********************************************************************************
 * Copyright 2017(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include "common.h"
#include "ad9528.h"
#include "mykonos.h"
#include "Mykonos_M3.h"
#include "mykonos_gpio.h"
#include "platform_drivers.h"
#include "parameters.h"
#include "util.h"
#ifdef ALTERA_PLATFORM
#include "clk_altera_a10_fpll.h"
#include "altera_adxcvr.h"
#else
#include "xil_cache.h"
#include "clk_axi_clkgen.h"
#include "axi_adxcvr.h"
#endif
#include "axi_jesd204_rx.h"
#include "axi_jesd204_tx.h"
#include "axi_dac_core.h"
#include "axi_adc_core.h"
#include "axi_dmac.h"

/******************************************************************************/
/************************ Variables Definitions *******************************/
/******************************************************************************/
extern ad9528Device_t clockAD9528_;
extern mykonosDevice_t mykDevice;

/***************************************************************************//**
 * @brief main
*******************************************************************************/
int main(void)
{
	ADI_ERR error;
	ad9528Device_t *clockAD9528_device = &clockAD9528_;
	mykonosErr_t mykError;
	const char *errorString;
	uint8_t pllLockStatus;
	uint8_t mcsStatus;
	uint8_t arm_major;
	uint8_t arm_minor;
	uint8_t arm_release;
	mykonosGpioErr_t mykGpioErr;
	uint32_t initCalMask = TX_BB_FILTER | ADC_TUNER | TIA_3DB_CORNER | DC_OFFSET |
			       TX_ATTENUATION_DELAY | RX_GAIN_DELAY | FLASH_CAL |
			       PATH_DELAY | TX_LO_LEAKAGE_INTERNAL | TX_QEC_INIT |
			       LOOPBACK_RX_LO_DELAY | LOOPBACK_RX_RX_QEC_INIT |
			       RX_LO_DELAY | RX_QEC_INIT ;
	uint8_t errorFlag = 0;
	uint8_t errorCode = 0;
	uint32_t initCalsCompleted;
	uint8_t framerStatus;
	uint8_t obsFramerStatus;
	uint8_t deframerStatus;
	uint32_t trackingCalMask = TRACK_ORX1_QEC | TRACK_ORX2_QEC | TRACK_RX1_QEC |
				   TRACK_RX2_QEC | TRACK_TX1_QEC | TRACK_TX2_QEC;
	uint32_t status;
	int32_t ret;
#ifdef ALTERA_PLATFORM
	struct altera_a10_fpll_init rx_device_clk_pll_init = {
		"rx_device_clk_pll",
		RX_A10_FPLL_BASEADDR,
		clockAD9528_device->outputSettings->outFrequency_Hz[1]
	};
	struct altera_a10_fpll_init tx_device_clk_pll_init = {
		"tx_device_clk_pll",
		TX_A10_FPLL_BASEADDR,
		clockAD9528_device->outputSettings->outFrequency_Hz[1]
	};
	struct altera_a10_fpll_init rx_os_device_clk_pll_init = {
		"rx_os_device_clk_pll",
		RX_OS_A10_FPLL_BASEADDR,
		clockAD9528_device->outputSettings->outFrequency_Hz[1]
	};
	struct altera_a10_fpll *rx_device_clk_pll;
	struct altera_a10_fpll *tx_device_clk_pll;
	struct altera_a10_fpll *rx_os_device_clk_pll;
#else
	struct axi_clkgen_init rx_clkgen_init = {
		"rx_clkgen",
		RX_CLKGEN_BASEADDR,
		clockAD9528_device->outputSettings->outFrequency_Hz[1]
	};
	struct axi_clkgen_init tx_clkgen_init = {
		"tx_clkgen",
		TX_CLKGEN_BASEADDR,
		clockAD9528_device->outputSettings->outFrequency_Hz[1]
	};
	struct axi_clkgen_init rx_os_clkgen_init = {
		"rx_os_clkgen",
		RX_OS_CLKGEN_BASEADDR,
		clockAD9528_device->outputSettings->outFrequency_Hz[1]
	};
	struct axi_clkgen *rx_clkgen;
	struct axi_clkgen *tx_clkgen;
	struct axi_clkgen *rx_os_clkgen;
#endif
	uint32_t rx_lane_rate_khz = mykDevice.rx->rxProfile->iqRate_kHz *
				    mykDevice.rx->framer->M * (20 /
						    hweight8(mykDevice.rx->framer->serializerLanesEnabled));
	uint32_t rx_div40_rate_hz = rx_lane_rate_khz * (1000 / 40);
	uint32_t tx_lane_rate_khz = mykDevice.tx->txProfile->iqRate_kHz *
				    mykDevice.tx->deframer->M * (20 /
						    hweight8(mykDevice.tx->deframer->deserializerLanesEnabled));
	uint32_t tx_div40_rate_hz = tx_lane_rate_khz * (1000 / 40);
	uint32_t rx_os_lane_rate_khz = mykDevice.obsRx->orxProfile->iqRate_kHz *
				       mykDevice.obsRx->framer->M * (20 /
						       hweight8(mykDevice.obsRx->framer->serializerLanesEnabled));
	uint32_t rx_os_div40_rate_hz = rx_os_lane_rate_khz * (1000 / 40);
	struct jesd204_rx_init rx_jesd_init = {
		"rx_jesd",
		RX_JESD_BASEADDR,
		4,
		32,
		1,
		rx_div40_rate_hz / 1000,
		rx_lane_rate_khz
	};
	struct jesd204_tx_init tx_jesd_init = {
		"tx_jesd",
		TX_JESD_BASEADDR,
		2,
		32,
		4,
		14,
		16,
		false,
		2,
		1,
		tx_div40_rate_hz / 1000,
		tx_lane_rate_khz
	};

	struct jesd204_rx_init rx_os_jesd_init = {
		"rx_os_jesd",
		RX_OS_JESD_BASEADDR,
		2,
		32,
		1,
		rx_os_div40_rate_hz / 1000,
		rx_os_lane_rate_khz
	};
	struct axi_jesd204_rx *rx_jesd;
	struct axi_jesd204_tx *tx_jesd;
	struct axi_jesd204_rx *rx_os_jesd;
#ifdef ALTERA_PLATFORM
	struct adxcvr_init rx_adxcvr_init = {
		"rx_adxcvr",
		RX_XCVR_BASEADDR,
		{RX_ADXCFG_0_BASEADDR, RX_ADXCFG_1_BASEADDR, 0, 0},
		0,
		rx_lane_rate_khz,
		mykDevice.clocks->deviceClock_kHz,
	};
	struct adxcvr_init tx_adxcvr_init = {
		"tx_adxcvr",
		TX_XCVR_BASEADDR,
		{TX_ADXCFG_0_BASEADDR, TX_ADXCFG_1_BASEADDR, TX_ADXCFG_2_BASEADDR, TX_ADXCFG_3_BASEADDR},
		TX_PLL_BASEADDR,
		tx_lane_rate_khz,
		mykDevice.clocks->deviceClock_kHz,
	};
	struct adxcvr_init rx_os_adxcvr_init = {
		"rx_os_adxcvr",
		RX_OS_XCVR_BASEADDR,
		{RX_OS_ADXCFG_0_BASEADDR, RX_OS_ADXCFG_1_BASEADDR, 0, 0},
		0,
		rx_os_lane_rate_khz,
		mykDevice.clocks->deviceClock_kHz,
	};
#else
	struct adxcvr_init rx_adxcvr_init = {
		"rx_adxcvr",
		RX_XCVR_BASEADDR,
		0,
		3,
		1,
		1,
		rx_lane_rate_khz,
		mykDevice.clocks->deviceClock_kHz,
	};
	struct adxcvr_init tx_adxcvr_init = {
		"tx_adxcvr",
		TX_XCVR_BASEADDR,
		3,
		3,
		0,
		0,
		tx_lane_rate_khz,
		mykDevice.clocks->deviceClock_kHz,
	};
	struct adxcvr_init rx_os_adxcvr_init = {
		"rx_os_adxcvr",
		RX_OS_XCVR_BASEADDR,
		0,
		3,
		1,
		1,
		rx_os_lane_rate_khz,
		mykDevice.clocks->deviceClock_kHz,
	};
#endif
	struct adxcvr *rx_adxcvr;
	struct adxcvr *tx_adxcvr;
	struct adxcvr *rx_os_adxcvr;
	struct axi_dac_init tx_dac_init = {
		"tx_dac",
		TX_CORE_BASEADDR,
		4
	};
	struct axi_dac *tx_dac;
	struct axi_adc_init rx_adc_init = {
		"rx_adc",
		RX_CORE_BASEADDR,
		4
	};
	struct axi_adc *rx_adc;
	struct axi_dmac_init rx_dmac_init = {
		"rx_dmac",
		RX_DMA_BASEADDR,
		DMA_DEV_TO_MEM,
		0
	};
	struct axi_dmac *rx_dmac;
#ifdef DAC_DMA_EXAMPLE
	struct axi_dmac_init tx_dmac_init = {
		"tx_dmac",
		TX_DMA_BASEADDR,
		DMA_MEM_TO_DEV,
		DMA_LAST,
	};
	struct axi_dmac *tx_dmac;
	extern const uint32_t sine_lut_iq[1024];
#endif
	uint32_t i;

	/* Allocating memory for the errorString */
	errorString = NULL;

	printf("Please wait...\n");

	ret = platform_init();
	if (ret != SUCCESS) {
		printf("error: platform_init() failed\n");
		goto error_0;
	}

	/**************************************************************************/
	/*****      System Clocks Initialization Initialization Sequence      *****/
	/**************************************************************************/

	/* Perform a hard reset on the AD9528 DUT */
	error = AD9528_resetDevice(clockAD9528_device);
	if (error != ADIERR_OK) {
		printf("AD9528_resetDevice() failed\n");
		error = ADIERR_FAILED;
		goto error_1;
	}

	error = AD9528_initDeviceDataStruct(clockAD9528_device,
					    clockAD9528_device->pll1Settings->vcxo_Frequency_Hz,
					    clockAD9528_device->pll1Settings->refA_Frequency_Hz,
					    clockAD9528_device->outputSettings->outFrequency_Hz[1]);
	if (error != ADIERR_OK) {
		printf("AD9528_initDeviceDataStruct() failed\n");
		error = ADIERR_FAILED;
		goto error_1;
	}

	/* Initialize the AD9528 by writing all SPI registers */
	error = AD9528_initialize(clockAD9528_device);
	if (error != ADIERR_OK)
		printf("WARNING: AD9528_initialize() issues. Possible cause: REF_CLK not connected.\n");

#ifdef ALTERA_PLATFORM
	/* Initialize A10 FPLLs */
	status = altera_a10_fpll_init(&rx_device_clk_pll,
				      &rx_device_clk_pll_init);
	if (status != SUCCESS) {
		printf("error: %s: altera_a10_fpll_init() failed\n",
		       rx_os_device_clk_pll_init.name);
		goto error_1;
	}
	status = altera_a10_fpll_init(&tx_device_clk_pll,
				      &tx_device_clk_pll_init);
	if (status != SUCCESS) {
		printf("error: %s: altera_a10_fpll_init() failed\n",
		       rx_os_device_clk_pll_init.name);
		goto error_2;
	}
	status = altera_a10_fpll_init(&rx_os_device_clk_pll,
				      &rx_os_device_clk_pll_init);
	if (status != SUCCESS) {
		printf("error: %s: altera_a10_fpll_init() failed\n",
		       rx_os_device_clk_pll_init.name);
		goto error_3;
	}

	altera_a10_fpll_disable(rx_device_clk_pll);
	status = altera_a10_fpll_set_rate(rx_device_clk_pll,
					  rx_div40_rate_hz);
	if (status != SUCCESS) {
		printf("error: %s: altera_a10_fpll_set_rate() failed\n",
		       rx_device_clk_pll->name);
		goto error_4;
	}
	altera_a10_fpll_enable(rx_device_clk_pll);
	altera_a10_fpll_disable(tx_device_clk_pll);
	status = altera_a10_fpll_set_rate(tx_device_clk_pll,
					  tx_div40_rate_hz);
	if (status != SUCCESS) {
		printf("error: %s: altera_a10_fpll_set_rate() failed\n",
		       tx_device_clk_pll->name);
		goto error_4;
	}
	altera_a10_fpll_enable(tx_device_clk_pll);
	altera_a10_fpll_disable(rx_os_device_clk_pll);
	status = altera_a10_fpll_set_rate(rx_os_device_clk_pll,
					  rx_os_div40_rate_hz);
	if (status != SUCCESS) {
		printf("error: %s: altera_a10_fpll_set_rate() failed\n",
		       rx_os_device_clk_pll->name);
		goto error_4;
	}
	altera_a10_fpll_enable(rx_os_device_clk_pll);
#else
	/* Initialize CLKGEN */
	status = axi_clkgen_init(&rx_clkgen, &rx_clkgen_init);
	if (status != SUCCESS) {
		printf("error: %s: axi_clkgen_init() failed\n", rx_clkgen_init.name);
		goto error_1;
	}
	status = axi_clkgen_init(&tx_clkgen, &tx_clkgen_init);
	if (status != SUCCESS) {
		printf("error: %s: axi_clkgen_init() failed\n", tx_clkgen_init.name);
		goto error_2;
	}
	status = axi_clkgen_init(&rx_os_clkgen, &rx_os_clkgen_init);
	if (status != SUCCESS) {
		printf("error: %s: axi_clkgen_set_rate() failed\n", rx_os_clkgen_init.name);
		goto error_3;
	}

	status = axi_clkgen_set_rate(rx_clkgen, rx_div40_rate_hz);
	if (status != SUCCESS) {
		printf("error: %s: axi_clkgen_set_rate() failed\n", rx_clkgen->name);
		goto error_4;
	}
	status = axi_clkgen_set_rate(tx_clkgen, tx_div40_rate_hz);
	if (status != SUCCESS) {
		printf("error: %s: axi_clkgen_set_rate() failed\n", tx_clkgen->name);
		goto error_4;
	}
	status = axi_clkgen_set_rate(rx_os_clkgen, rx_os_div40_rate_hz);
	if (status != SUCCESS) {
		printf("error: %s: axi_clkgen_set_rate() failed\n", rx_os_clkgen->name);
		goto error_4;
	}
#endif

	/* Initialize JESDs */
	status = axi_jesd204_rx_init(&rx_jesd, &rx_jesd_init);
	if (status != SUCCESS) {
		printf("error: %s: axi_jesd204_rx_init() failed\n", rx_jesd_init.name);
		goto error_4;
	}
	status = axi_jesd204_tx_init(&tx_jesd, &tx_jesd_init);
	if (status != SUCCESS) {
		printf("error: %s: axi_jesd204_rx_init() failed\n", rx_jesd_init.name);
		goto error_5;
	}
	status = axi_jesd204_rx_init(&rx_os_jesd, &rx_os_jesd_init);
	if (status != SUCCESS) {
		printf("error: %s: axi_jesd204_rx_init() failed\n", rx_jesd_init.name);
		goto error_6;
	}

	/* Initialize ADXCVRs */
	status = adxcvr_init(&rx_adxcvr, &rx_adxcvr_init);
	if (status != SUCCESS) {
		printf("error: %s: adxcvr_init() failed\n", rx_adxcvr_init.name);
		goto error_7;
	}
	status = adxcvr_init(&tx_adxcvr, &tx_adxcvr_init);
	if (status != SUCCESS) {
		printf("error: %s: adxcvr_init() failed\n", tx_adxcvr_init.name);
		goto error_8;
	}
	status = adxcvr_init(&rx_os_adxcvr, &rx_os_adxcvr_init);
	if (status != SUCCESS) {
		printf("error: %s: adxcvr_init() failed\n", rx_os_adxcvr_init.name);
		goto error_9;
	}

	/*************************************************************************/
	/*****                Mykonos Initialization Sequence                *****/
	/*************************************************************************/

	/* Perform a hard reset on the MYKONOS DUT (Toggle RESETB pin on device) */
	if ((mykError = MYKONOS_resetDevice(&mykDevice)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_initialize(&mykDevice)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*************************************************************************/
	/*****                Mykonos CLKPLL Status Check                    *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_checkPllsLockStatus(&mykDevice,
			&pllLockStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*************************************************************************/
	/*****                Mykonos Perform MultiChip Sync                 *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_enableMultichipSync(&mykDevice, 1,
			&mcsStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/* Minimum 3 SYSREF pulses from Clock Device has to be produced for MulticChip Sync */

	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(1);
	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(1);
	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(1);
	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(1);

	/*************************************************************************/
	/*****                Mykonos Verify MultiChip Sync                 *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_enableMultichipSync(&mykDevice, 0,
			&mcsStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mcsStatus & 0x0B) == 0x0B)
		printf("MCS successful\n");
	else
		printf("MCS failed\n");

	/*************************************************************************/
	/*****                Mykonos Load ARM file                          *****/
	/*************************************************************************/

	if (pllLockStatus & 0x01) {
		printf("CLKPLL locked\n");
		if ((mykError = MYKONOS_initArm(&mykDevice)) != MYKONOS_ERR_OK) {
			errorString = getMykonosErrorMessage(mykError);
			goto error_11;
		}

		if ((mykError = MYKONOS_loadArmFromBinary(&mykDevice,
				&firmware_Mykonos_M3_bin[0], firmware_Mykonos_M3_bin_len)) != MYKONOS_ERR_OK) {
			errorString = getMykonosErrorMessage(mykError);
			goto error_11;
		}
	} else {
		printf("CLKPLL not locked (0x%x)\n", pllLockStatus);
		error = ADIERR_FAILED;
		goto error_2;
	}

	/* Read back the version of the ARM binary loaded into the Mykonos ARM memory */
	if ((mykError = MYKONOS_getArmVersion(&mykDevice, &arm_major, &arm_minor,
					      &arm_release, NULL)) == MYKONOS_ERR_OK)
		printf("AD9371 ARM version %d.%d.%d\n", arm_major, arm_minor, arm_release);

	/*************************************************************************/
	/*****                Mykonos Set RF PLL Frequencies                 *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_setRfPllFrequency(&mykDevice, RX_PLL,
			mykDevice.rx->rxPllLoFrequency_Hz)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setRfPllFrequency(&mykDevice, TX_PLL,
			mykDevice.tx->txPllLoFrequency_Hz)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setRfPllFrequency(&mykDevice, SNIFFER_PLL,
			mykDevice.obsRx->snifferPllLoFrequency_Hz)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/* Wait 200ms for PLLs to lock */
	mdelay(200);

	if ((mykError = MYKONOS_checkPllsLockStatus(&mykDevice,
			&pllLockStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((pllLockStatus & 0x0F) == 0x0F)
		printf("PLLs locked\n");
	else {
		printf("PLLs not locked (0x%x)\n", pllLockStatus);
		error = ADIERR_FAILED;
		goto error_2;
	}

	/*************************************************************************/
	/*****                Mykonos Set GPIOs                              *****/
	/*************************************************************************/

	if ((mykGpioErr = MYKONOS_setRx1GainCtrlPin(&mykDevice, 0, 0, 0, 0,
			  0)) != MYKONOS_ERR_GPIO_OK) {
		errorString = getGpioMykonosErrorMessage(mykGpioErr);
		goto error_11;
	}

	if ((mykGpioErr = MYKONOS_setRx2GainCtrlPin(&mykDevice, 0, 0, 0, 0,
			  0)) != MYKONOS_ERR_GPIO_OK) {
		errorString = getGpioMykonosErrorMessage(mykGpioErr);
		goto error_11;
	}

	if ((mykGpioErr = MYKONOS_setTx1AttenCtrlPin(&mykDevice, 0, 0, 0, 0,
			  0)) != MYKONOS_ERR_GPIO_OK) {
		errorString = getGpioMykonosErrorMessage(mykGpioErr);
		goto error_11;
	}

	if ((mykGpioErr = MYKONOS_setTx2AttenCtrlPin(&mykDevice, 0, 0, 0,
			  0)) != MYKONOS_ERR_GPIO_OK) {
		errorString = getGpioMykonosErrorMessage(mykGpioErr);
		goto error_11;
	}

	if ((mykGpioErr = MYKONOS_setupGpio(&mykDevice)) != MYKONOS_ERR_GPIO_OK) {
		errorString = getGpioMykonosErrorMessage(mykGpioErr);
		goto error_11;
	}

	/*************************************************************************/
	/*****                Mykonos Set manual gains values                *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_setRx1ManualGain(&mykDevice, 255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setRx2ManualGain(&mykDevice, 255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setObsRxManualGain(&mykDevice, OBS_RX1_TXLO,
			255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setObsRxManualGain(&mykDevice, OBS_RX2_TXLO,
			255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setObsRxManualGain(&mykDevice, OBS_SNIFFER_A,
			255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setObsRxManualGain(&mykDevice, OBS_SNIFFER_B,
			255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_setObsRxManualGain(&mykDevice, OBS_SNIFFER_C,
			255)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*************************************************************************/
	/*****                Mykonos Initialize attenuations                *****/
	/*************************************************************************/

	mykError = MYKONOS_setTx1Attenuation(&mykDevice, 10000);
	if (mykError != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	mykError = MYKONOS_setTx2Attenuation(&mykDevice, 10000);
	if (mykError != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*************************************************************************/
	/*****           Mykonos ARM Initialization Calibrations             *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_runInitCals(&mykDevice,
					    (initCalMask & ~TX_LO_LEAKAGE_EXTERNAL))) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_waitInitCals(&mykDevice, 60000, &errorFlag,
					     &errorCode)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((errorFlag != 0) || (errorCode != 0)) {
		/*** < Info: abort init cals > ***/
		if ((mykError = MYKONOS_abortInitCals(&mykDevice,
						      &initCalsCompleted)) != MYKONOS_ERR_OK) {
			errorString = getMykonosErrorMessage(mykError);
			goto error_11;
		}
		if (initCalsCompleted)
			printf("Completed calibrations: %x\n", (unsigned int)initCalsCompleted);
	} else
		printf("Calibrations completed successfully\n");

	/*************************************************************************/
	/*****  Mykonos ARM Initialization External LOL Calibrations with PA *****/
	/*************************************************************************/

	/* Please ensure PA is enabled operational at this time */
	if (initCalMask & TX_LO_LEAKAGE_EXTERNAL) {
		if ((mykError = MYKONOS_runInitCals(&mykDevice,
						    TX_LO_LEAKAGE_EXTERNAL)) != MYKONOS_ERR_OK) {
			errorString = getMykonosErrorMessage(mykError);
			goto error_11;
		}

		if ((mykError = MYKONOS_waitInitCals(&mykDevice, 60000, &errorFlag,
						     &errorCode)) != MYKONOS_ERR_OK) {
			errorString = getMykonosErrorMessage(mykError);
			goto error_11;
		}

		if ((errorFlag != 0) || (errorCode != 0)) {
			/*** < Info: abort init cals > ***/
			if ((mykError = MYKONOS_abortInitCals(&mykDevice,
							      &initCalsCompleted)) != MYKONOS_ERR_OK) {
				errorString = getMykonosErrorMessage(mykError);
				goto error_11;
			}
		} else
			printf("External LOL Calibrations completed successfully\n");
	}

	/*************************************************************************/
	/*****             SYSTEM JESD bring up procedure                    *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_enableSysrefToRxFramer(&mykDevice,
			1)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}
	/*** < Info: Mykonos is waiting for sysref in order to start
	 * transmitting CGS from the RxFramer> ***/

	if ((mykError = MYKONOS_enableSysrefToObsRxFramer(&mykDevice,
			1)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}
	/*** < Info: Mykonos is waiting for sysref in order to start
	 * transmitting CGS from the ObsRxFramer> ***/

	if ((mykError = MYKONOS_enableSysrefToDeframer(&mykDevice,
			0)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	if ((mykError = MYKONOS_resetDeframer(&mykDevice)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

#ifndef ALTERA_PLATFORM
	status = adxcvr_clk_enable(tx_adxcvr);
	if (status != SUCCESS) {
		printf("error: %s: adxcvr_clk_enable() failed\n", tx_adxcvr->name);
		goto error_10;
	}
#endif
	axi_jesd204_tx_lane_clk_enable(tx_jesd);

	if ((mykError = MYKONOS_enableSysrefToDeframer(&mykDevice,
			1)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*************************************************************************/
	/*****            Enable SYSREF to Mykonos and BBIC                  *****/
	/*************************************************************************/

	/* Request a SYSREF from the AD9528 */
	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(1);

	/*** < Info: Mykonos is actively transmitting CGS from the RxFramer> ***/

	/*** < Info: Mykonos is actively transmitting CGS from the ObsRxFramer> ***/

#ifndef ALTERA_PLATFORM
	status = adxcvr_clk_enable(rx_adxcvr);
	if (status != SUCCESS) {
		printf("error: %s: adxcvr_clk_enable() failed\n", rx_adxcvr->name);
		goto error_10;
	}
#endif
	axi_jesd204_rx_lane_clk_enable(rx_jesd);
#ifndef ALTERA_PLATFORM
	status = adxcvr_clk_enable(rx_os_adxcvr);
	if (status != SUCCESS) {
		printf("error: %s: adxcvr_clk_enable() failed\n", rx_os_adxcvr->name);
		goto error_10;
	}
#endif
	axi_jesd204_rx_lane_clk_enable(rx_os_jesd);

	/* Request two SYSREFs from the AD9528 */
	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(1);
	AD9528_requestSysref(clockAD9528_device, 1);
	mdelay(5);

	/*************************************************************************/
	/*****               Check Mykonos Framer Status                     *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_readRxFramerStatus(&mykDevice,
			&framerStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	} else if (framerStatus != 0x3E)
		printf("RxFramerStatus = 0x%x\n", framerStatus);

	if ((mykError = MYKONOS_readOrxFramerStatus(&mykDevice,
			&obsFramerStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	} else if (obsFramerStatus != 0x3E)
		printf("OrxFramerStatus = 0x%x\n", obsFramerStatus);

	/*************************************************************************/
	/*****               Check Mykonos Deframer Status                   *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_readDeframerStatus(&mykDevice,
			&deframerStatus)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	} else if (deframerStatus != 0x28)
		printf("DeframerStatus = 0x%x\n", deframerStatus);

	/*************************************************************************/
	/*****           Mykonos enable tracking calibrations                *****/
	/*************************************************************************/

	if ((mykError = MYKONOS_enableTrackingCals(&mykDevice,
			trackingCalMask)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*** < Info: Allow Rx1/2 QEC tracking and Tx1/2 QEC tracking to run when in the radioOn state
	     *  Tx calibrations will only run if radioOn and the obsRx path is set to OBS_INTERNAL_CALS > ***/

	/*** < Info: Function to turn radio on, Enables transmitters and receivers
	 * that were setup during MYKONOS_initialize() > ***/
	if ((mykError = MYKONOS_radioOn(&mykDevice)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	/*** < Info: Allow TxQEC to run when User: is not actively using ORx receive path > ***/
	if ((mykError = MYKONOS_setObsRxPathSource(&mykDevice,
			OBS_RXOFF)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}
	if ((mykError = MYKONOS_setObsRxPathSource(&mykDevice,
			OBS_INTERNALCALS)) != MYKONOS_ERR_OK) {
		errorString = getMykonosErrorMessage(mykError);
		goto error_11;
	}

	axi_jesd204_rx_watchdog(rx_jesd);
	axi_jesd204_rx_watchdog(rx_os_jesd);

	mdelay(1000);

	/* Print JESD status */
	axi_jesd204_rx_status_read(rx_jesd);
	for (i = 0; i < rx_jesd->num_lanes; i++)
		axi_jesd204_rx_laneinfo_read(rx_jesd, i);
	axi_jesd204_tx_status_read(tx_jesd);
	axi_jesd204_rx_status_read(rx_os_jesd);
	for (i = 0; i < rx_os_jesd->num_lanes; i++)
		axi_jesd204_rx_laneinfo_read(rx_os_jesd, i);

	/* Initialize the DAC DDS */
	axi_dac_init(&tx_dac, &tx_dac_init);

	/* Initialize the ADC core */
	axi_adc_init(&rx_adc, &rx_adc_init);

#ifdef DAC_DMA_EXAMPLE
	axi_dac_load_custom_data(tx_dac, sine_lut_iq,
				 ARRAY_SIZE(sine_lut_iq),
				 DDR_MEM_BASEADDR + 0xA000000);
	axi_dmac_init(&tx_dmac, &tx_dmac_init);
	axi_dmac_transfer(tx_dmac, DDR_MEM_BASEADDR + 0xA000000,
			  sizeof(sine_lut_iq) * 2);
#endif
	mdelay(1000);

	/* Initialize the DMAC and transfer 16384 samples from ADC to MEM */
	axi_dmac_init(&rx_dmac, &rx_dmac_init);
	axi_dmac_transfer(rx_dmac,
			  DDR_MEM_BASEADDR + 0x800000,
			  16384 * 8);
#ifndef ALTERA_PLATFORM
	Xil_DCacheInvalidateRange(DDR_MEM_BASEADDR + 0x800000, 16384 * 8);
#endif

	printf("Done\n");

	return 0;

error_11:
	printf("%s", errorString);
#ifndef ALTERA_PLATFORM
error_10:
#endif
	adxcvr_remove(rx_os_adxcvr);
error_9:
	adxcvr_remove(tx_adxcvr);
error_8:
	adxcvr_remove(rx_adxcvr);
error_7:
	axi_jesd204_rx_remove(rx_os_jesd);
error_6:
	axi_jesd204_tx_remove(tx_jesd);
error_5:
	axi_jesd204_rx_remove(rx_jesd);
error_4:
#ifdef ALTERA_PLATFORM
	altera_a10_fpll_remove(rx_os_device_clk_pll);
#else
	axi_clkgen_remove(rx_os_clkgen);
#endif
error_3:
#ifdef ALTERA_PLATFORM
	altera_a10_fpll_remove(tx_device_clk_pll);
#else
	axi_clkgen_remove(tx_clkgen);
#endif
error_2:
#ifdef ALTERA_PLATFORM
	altera_a10_fpll_remove(rx_device_clk_pll);
#else
	axi_clkgen_remove(rx_clkgen);
#endif
error_1:
	platform_remove();
error_0:
	return FAILURE;
}
