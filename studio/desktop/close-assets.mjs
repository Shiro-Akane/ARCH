/** Stop owned asset/proxy sockets only after Host work has drained. */
export function closeAssets(server){
 return new Promise((resolve,reject)=>{
  server.close(error=>error?reject(error):resolve());
  // The still-open renderer may keep polling over an existing keep-alive socket.
  // Stop accepting first, then close those sockets; no scientific worker is owned here.
  server.closeAllConnections();
 });
}
