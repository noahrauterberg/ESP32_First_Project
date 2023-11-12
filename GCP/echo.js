const functions = require('@google-cloud/functions-framework');

functions.http('echoRequest', (req, res) => {
  if (req.get('Content-Type') === 'application/json') {
    // parse received json body to string
    res.send(`Echo: ${JSON.stringify(req.body)}`);
  } else if (req.get('Content-Type') === 'text/plain') {
    // echo plain text
    res.send(`Echo: ${req.body}`);
  } else {
    // if there is no header, we echo the message
    res.send(`Echo: ${req.body}`);
  }
});
