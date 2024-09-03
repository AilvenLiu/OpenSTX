from ib_insync import *
import logging

# Configure logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

def request_l2_data(ib, contract, numRows, isSmartDepth, mktDepthOptions):
    logger.info(f"Requesting L2 data for contract: {contract.symbol} with numRows={numRows}, isSmartDepth={isSmartDepth}, mktDepthOptions={mktDepthOptions}")
    ticker = ib.reqMktDepth(contract, numRows=numRows, isSmartDepth=isSmartDepth, mktDepthOptions=mktDepthOptions)
    ib.sleep(5)  # Wait for data to be received

    # Check if L2 data is received
    if ticker.domBids or ticker.domAsks:
        logger.info(f"L2 data received for {contract.symbol} with numRows={numRows}, isSmartDepth={isSmartDepth}")
        logger.info(f"Bids: {ticker.domBids}")
        logger.info(f"Asks: {ticker.domAsks}")
        return True
    else:
        logger.warning(f"No L2 data received for {contract.symbol} with numRows={numRows}, isSmartDepth={isSmartDepth}")
        return False

def main():
    # Connect to IB Gateway or TWS
    ib = IB()
    try:
        ib.connect('127.0.0.1', 7496, clientId=3)  # Adjust the host, port, and clientId as needed
        logger.info("Connected to IB Gateway/TWS")

        # Define the contract for SPY
        contract = Stock('SPY', 'ARCA', 'USD')

        # Possible parameter combinations
        numRows_options = [5, 10, 20, 30, 50]
        isSmartDepth_options = [True, False]
        mktDepthOptions_options = [None, []]

        # Track successful combinations
        successful_combinations = []

        # Try all combinations
        for numRows in numRows_options:
            for isSmartDepth in isSmartDepth_options:
                for mktDepthOptions in mktDepthOptions_options:
                    if request_l2_data(ib, contract, numRows, isSmartDepth, mktDepthOptions):
                        successful_combinations.append((numRows, isSmartDepth, mktDepthOptions))

        # Log successful combinations
        if successful_combinations:
            logger.info("Successful L2 data requests found with the following parameter combinations:")
            for combination in successful_combinations:
                logger.info(f"numRows={combination[0]}, isSmartDepth={combination[1]}, mktDepthOptions={combination[2]}")
        else:
            logger.warning("No successful L2 data requests found with any parameter combinations.")

    except Exception as e:
        logger.error(f"Error: {e}")
    finally:
        ib.disconnect()
        logger.info("Disconnected from IB Gateway/TWS")

if __name__ == "__main__":
    main()