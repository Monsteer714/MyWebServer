/**
 * script.js - MIME type test file
 * Expected Content-Type: application/javascript
 */
console.log('script.js loaded successfully!');

function testFunction() {
    return {
        status: 'ok',
        message: 'JavaScript MIME type test passed',
        timestamp: new Date().toISOString()
    };
}
